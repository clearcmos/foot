#include "tab.h"

#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define LOG_MODULE "tab"
#define LOG_ENABLE_DBG 0
#include "log.h"

#include "config.h"
#include "fdm.h"
#include "macros.h"
#include "render.h"
#include "reaper.h"
#include "shm.h"
#include "terminal.h"
#include "slave.h"
#include "vt.h"
#include "wayland.h"
#include "xmalloc.h"

void
tab_bar_init(struct tab_bar *tb, int undo_timeout_ms)
{
    *tb = (struct tab_bar){
        .tabs = tll_init(),
        .active = NULL,
        .closed = tll_init(),
        .surface = NULL,
        .chain = NULL,
        .font = NULL,
        .height = 0,
        .tab_count = 0,
        .undo_timeout_ms = undo_timeout_ms,
        .hovered_tab = -1,
        .dirty = true,
    };
}

void
tab_bar_destroy(struct tab_bar *tb, struct fdm *fdm)
{
    tll_foreach(tb->tabs, it) {
        free(it->item.title);
        tll_remove(tb->tabs, it);
    }

    tll_foreach(tb->closed, it) {
        if (it->item.timer_fd >= 0) {
            fdm_del(fdm, it->item.timer_fd);
            close(it->item.timer_fd);
        }
        free(it->item.title);
        free(it->item.scrollback);
        free(it->item.cwd);
        tll_remove(tb->closed, it);
    }

    if (tb->font != NULL) {
        fcft_destroy(tb->font);
        tb->font = NULL;
    }

    if (tb->surface != NULL) {
        wayl_win_subsurface_destroy(tb->surface);
        free(tb->surface);
        tb->surface = NULL;
    }

    if (tb->chain != NULL) {
        shm_chain_free(tb->chain);
        tb->chain = NULL;
    }

    tb->active = NULL;
    tb->tab_count = 0;
}

void
tab_bar_add_initial(struct tab_bar *tb, struct terminal *term)
{
    const char *title = term->window_title != NULL ? term->window_title : "";
    tll_push_back(tb->tabs, ((struct tab){
        .term = term,
        .title = xstrdup(title),
        .urgent = false,
    }));
    tb->active = &tll_back(tb->tabs);
    tb->tab_count = 1;
    tb->dirty = true;
}

static struct tab *
find_tab_for_term(struct wl_window *win, struct terminal *term)
{
    tll_foreach(win->tab_bar.tabs, it) {
        if (it->item.term == term)
            return &it->item;
    }
    return NULL;
}

static void
do_tab_switch(struct wl_window *win, struct tab *new_tab)
{
    if (win->tab_bar.active == new_tab)
        return;

    struct terminal *old_term = win->tab_bar.active->term;
    struct terminal *new_term = new_tab->term;

    /* Suppress rendering on old tab */
    old_term->render.refresh.grid = false;

    /* Update active tab */
    win->tab_bar.active = new_tab;
    win->term = new_term;

    /* Copy active_surface from old terminal so pointer state is consistent */
    new_term->active_surface = old_term->active_surface;

    /* Sync font state from old tab if zoom changed */
    if (new_term->cell_width != old_term->cell_width ||
        new_term->cell_height != old_term->cell_height)
    {
        /* Copy font sizes so reload_fonts produces matching results */
        for (size_t i = 0; i < 4; i++) {
            const struct config_font_list *fl = &old_term->conf->fonts[i];
            for (size_t j = 0; j < fl->count; j++)
                new_term->font_sizes[i][j] = old_term->font_sizes[i][j];

            fcft_destroy(new_term->fonts[i]);
            new_term->fonts[i] = old_term->fonts[i] != NULL
                ? fcft_clone(old_term->fonts[i]) : NULL;
        }
        new_term->cell_width = old_term->cell_width;
        new_term->cell_height = old_term->cell_height;
        new_term->font_x_ofs = old_term->font_x_ofs;
        new_term->font_y_ofs = old_term->font_y_ofs;
        new_term->font_baseline = old_term->font_baseline;
        new_term->font_line_height = old_term->font_line_height;
    }

    /* Sync dimensions: resize new tab to match current window */
    new_term->scale = old_term->scale;
    {
        int logical_width = (int)roundf(old_term->width / old_term->scale);
        int logical_height = (int)roundf(old_term->height / old_term->scale);
        render_resize(new_term, logical_width, logical_height, RESIZE_FORCE);
    }

    /* Update seat focus to point to new terminal */
    tll_foreach(new_term->wl->seats, it) {
        if (it->item.kbd_focus == old_term)
            it->item.kbd_focus = new_term;
        if (it->item.mouse_focus == old_term)
            it->item.mouse_focus = new_term;
        if (it->item.ime_focus == old_term)
            it->item.ime_focus = new_term;
    }

    /* Trigger full redraw of the new tab */
    term_damage_all(new_term);
    render_refresh(new_term);
    win->tab_bar.dirty = true;

    /* Update window title */
    if (new_term->window_title != NULL)
        xdg_toplevel_set_title(win->xdg_toplevel, new_term->window_title);

    LOG_DBG("switched to tab %d", tab_index_of(win, new_term));
}

bool
tab_new(struct terminal *term)
{
    struct wl_window *win = term->window;
    struct wayland *wayl = term->wl;
    const struct config *conf = term->conf;

    struct terminal *new_term = term_init(
        conf, term->fdm, term->reaper, wayl,
        term->foot_exe, term->cwd,
        NULL,  /* token */
        NULL,  /* pty_path */
        0, NULL, NULL,  /* argc, argv, envp - use defaults from conf */
        term->shutdown.cb, term->shutdown.cb_data,
        win);  /* reuse existing window */

    if (new_term == NULL) {
        LOG_ERR("failed to create new terminal for tab");
        return false;
    }

    /* Add to tab list */
    const char *title = new_term->window_title != NULL
        ? new_term->window_title : "shell";
    tll_push_back(win->tab_bar.tabs, ((struct tab){
        .term = new_term,
        .title = xstrdup(title),
        .urgent = false,
    }));
    win->tab_bar.tab_count++;
    win->tab_bar.dirty = true;

    /* Create tab bar subsurface if this is the second tab */
    if (win->tab_bar.tab_count == 2 && win->tab_bar.surface == NULL) {
        win->tab_bar.surface = xmalloc(sizeof(*win->tab_bar.surface));
        memset(win->tab_bar.surface, 0, sizeof(*win->tab_bar.surface));
        if (!wayl_win_subsurface_new(win, win->tab_bar.surface, true)) {
            LOG_ERR("failed to create tab bar subsurface");
            free(win->tab_bar.surface);
            win->tab_bar.surface = NULL;
        }
    }

    /* Create buffer chain for tab bar rendering */
    if (win->tab_bar.chain == NULL) {
        win->tab_bar.chain = shm_chain_new(
            wayl, false, 1, SHM_BITS_8, NULL, NULL);
    }

    /*
     * The new terminal shares the existing (already configured) window,
     * but has no grid yet. We need to:
     * 1. Enable its PTY FDM callback (term_window_configured does this)
     * 2. Resize it to match the current window size (allocates the grid)
     */
    term_window_configured(new_term);

    /* render_resize expects logical (pre-scale) dimensions */
    int logical_width = (int)roundf(term->width / term->scale);
    int logical_height = (int)roundf(term->height / term->scale);
    render_resize(new_term, logical_width, logical_height, RESIZE_FORCE);

    /* Resize all existing tabs to account for the (possibly new) tab bar */
    tll_foreach(win->tab_bar.tabs, it) {
        struct terminal *t = it->item.term;
        if (t != new_term && t->width > 0) {
            int lw = (int)roundf(t->width / t->scale);
            int lh = (int)roundf(t->height / t->scale);
            render_resize(t, lw, lh, RESIZE_FORCE);
        }
    }

    /* Switch to the new tab */
    do_tab_switch(win, &tll_back(win->tab_bar.tabs));

    LOG_INFO("new tab created (total: %d)", win->tab_bar.tab_count);
    return true;
}

bool
tab_close_active(struct terminal *term)
{
    struct wl_window *win = term->window;
    struct tab_bar *tb = &win->tab_bar;

    if (tb->tab_count <= 1)
        return false;  /* Last tab - caller should close window */

    /* Find the active tab in the list */
    struct tab *closing = tb->active;
    struct terminal *closing_term = closing->term;

    /* Find previous tab (left) to switch to, fall back to next (right) */
    struct tab *prev_tab = NULL;
    struct tab *next_tab = NULL;
    bool found = false;
    tll_foreach(tb->tabs, it) {
        if (&it->item == closing) {
            found = true;
            continue;
        }
        if (!found) {
            prev_tab = &it->item;
        } else if (next_tab == NULL) {
            next_tab = &it->item;
        }
    }

    struct tab *target = prev_tab != NULL ? prev_tab : next_tab;
    xassert(target != NULL);

    /* Switch first */
    do_tab_switch(win, target);

    /*
     * Kill the shell and clean up FDs manually, but keep the terminal
     * struct alive with its grid/scrollback intact.
     * We avoid term_shutdown to prevent the async destroy chain.
     */

    /* Remove from wayl->terms so render loop skips it */
    tll_foreach(closing_term->wl->terms, it) {
        if (it->item == closing_term) {
            tll_remove(closing_term->wl->terms, it);
            break;
        }
    }

    /* Remove reaper callback BEFORE killing, so fdm_client_terminated
     * never fires and the terminal struct isn't destroyed */
    if (closing_term->slave > 0)
        reaper_del(closing_term->reaper, closing_term->slave);

    /* Unregister PTY from event loop and close it */
    fdm_del(closing_term->fdm, closing_term->ptmx);
    close(closing_term->ptmx);
    closing_term->ptmx = -1;

    /* Kill the shell and all its children */
    if (closing_term->slave > 0) {
        kill(-closing_term->slave, SIGHUP);
        closing_term->slave = -1;
    }

    closing_term->window = NULL;

    tll_push_back(tb->closed, ((struct closed_tab){
        .term = closing_term,
        .title = closing->title != NULL ? xstrdup(closing->title) : NULL,
        .timer_fd = -1,
        .scrollback = NULL,
        .scrollback_len = 0,
        .cwd = NULL,
    }));

    LOG_INFO("tab closed, grid preserved for undo");

    /* Remove from tab list */
    tll_foreach(tb->tabs, it) {
        if (&it->item == closing) {
            free(it->item.title);
            tll_remove(tb->tabs, it);
            break;
        }
    }

    tb->tab_count--;
    tb->dirty = true;

    /* Hide tab bar when down to 1 tab */
    if (tb->tab_count <= 1 && tb->surface != NULL) {
        wl_surface_attach(tb->surface->surface.surf, NULL, 0, 0);
        wl_surface_commit(tb->surface->surface.surf);

        /* Re-render grid to reclaim tab bar space */
        struct terminal *active = tb->active->term;
        int logical_width = (int)roundf(active->width / active->scale);
        int logical_height = (int)roundf(active->height / active->scale);
        render_resize(active, logical_width, logical_height, RESIZE_FORCE);
        term_damage_all(active);
        render_refresh(active);
    }

    LOG_INFO("tab closed (remaining: %d)", tb->tab_count);
    return true;
}

void
tab_next(struct terminal *term)
{
    struct wl_window *win = term->window;
    struct tab_bar *tb = &win->tab_bar;

    if (tb->tab_count <= 1)
        return;

    bool found = false;
    tll_foreach(tb->tabs, it) {
        if (&it->item == tb->active) {
            found = true;
            continue;
        }
        if (found) {
            do_tab_switch(win, &it->item);
            return;
        }
    }

    /* Wrap to first */
    do_tab_switch(win, &tll_front(tb->tabs));
}

void
tab_prev(struct terminal *term)
{
    struct wl_window *win = term->window;
    struct tab_bar *tb = &win->tab_bar;

    if (tb->tab_count <= 1)
        return;

    struct tab *prev = NULL;
    tll_foreach(tb->tabs, it) {
        if (&it->item == tb->active) {
            if (prev != NULL) {
                do_tab_switch(win, prev);
                return;
            }
            break;
        }
        prev = &it->item;
    }

    /* Wrap to last */
    do_tab_switch(win, &tll_back(tb->tabs));
}

void
tab_switch_to(struct wl_window *win, int index)
{
    int i = 0;
    tll_foreach(win->tab_bar.tabs, it) {
        if (i == index) {
            do_tab_switch(win, &it->item);
            return;
        }
        i++;
    }
}

bool
tab_undo_close(struct terminal *term)
{
    struct wl_window *win = term->window;
    struct tab_bar *tb = &win->tab_bar;

    if (tll_length(tb->closed) == 0)
        return false;

    /* Get the most recently closed tab (last in list) */
    struct closed_tab ct = tll_back(tb->closed);

    if (ct.term == NULL)
        return false;

    struct terminal *restored = ct.term;
    restored->window = win;

    /* Re-add to wayl->terms */
    tll_push_back(restored->wl->terms, restored);

    /* Create tab bar subsurface if going from 1 to 2 tabs */
    if (tb->tab_count == 1 && tb->surface == NULL) {
        tb->surface = xmalloc(sizeof(*tb->surface));
        memset(tb->surface, 0, sizeof(*tb->surface));
        if (!wayl_win_subsurface_new(win, tb->surface, true)) {
            LOG_ERR("failed to create tab bar subsurface");
            free(tb->surface);
            tb->surface = NULL;
        }
    }
    if (tb->chain == NULL) {
        tb->chain = shm_chain_new(
            restored->wl, false, 1, SHM_BITS_8, NULL, NULL);
    }

    /* Add back to tab list */
    tll_push_back(tb->tabs, ((struct tab){
        .term = restored,
        .title = ct.title != NULL ? xstrdup(ct.title) : xstrdup("restored"),
        .urgent = false,
    }));
    tb->tab_count++;
    tb->dirty = true;

    /* Spawn a new PTY and shell for the restored terminal */
    {
        int ptmx = posix_openpt(O_RDWR | O_NOCTTY);
        if (ptmx >= 0) {
            int flags = fcntl(ptmx, F_GETFL);
            fcntl(ptmx, F_SETFL, flags | O_NONBLOCK);

            struct winsize ws = {
                .ws_row = restored->rows,
                .ws_col = restored->cols,
            };
            ioctl(ptmx, TIOCSWINSZ, &ws);

            restored->ptmx = ptmx;
            fdm_add(restored->fdm, ptmx, EPOLLIN, &fdm_ptmx, restored);

            restored->slave = slave_spawn(
                ptmx, 0, restored->cwd, NULL, NULL,
                &restored->conf->env_vars, restored->conf->term,
                restored->conf->shell, restored->conf->login_shell,
                &restored->conf->notifications);

            if (restored->slave > 0) {
                reaper_add(restored->reaper, restored->slave,
                           &fdm_client_terminated, restored);
            }

            restored->shutdown.in_progress = false;
            restored->shutdown.client_has_terminated = false;
        }
    }

    /* Switch to it and force resize to recalculate margins for tab bar */
    do_tab_switch(win, &tll_back(tb->tabs));
    {
        int lw = (int)roundf(restored->width / restored->scale);
        int lh = (int)roundf(restored->height / restored->scale);
        render_resize(restored, lw, lh, RESIZE_FORCE);
    }

    /* Clean up closed tab entry */
    free(ct.title);
    tll_foreach(tb->closed, it) {
        if (it->item.term == restored) {
            tll_remove(tb->closed, it);
            break;
        }
    }

    LOG_INFO("tab restored from undo queue (total: %d)", tb->tab_count);
    return true;
}

void
tab_update_title(struct wl_window *win, struct terminal *term,
                 const char *title)
{
    struct tab *tab = find_tab_for_term(win, term);
    if (tab == NULL)
        return;

    free(tab->title);
    tab->title = xstrdup(title);
    win->tab_bar.dirty = true;
}

int
tab_index_of(const struct wl_window *win, const struct terminal *term)
{
    int i = 0;
    tll_foreach(win->tab_bar.tabs, it) {
        if (it->item.term == term)
            return i;
        i++;
    }
    return -1;
}

int
tab_count(const struct wl_window *win)
{
    return win->tab_bar.tab_count;
}

int
tab_bar_height(const struct terminal *term)
{
    if (term->window == NULL)
        return 0;
    if (term->window->tab_bar.tab_count <= 1)
        return 0;

    /* Fixed tab bar height in pixels, scaled for monitor */
    return (int)roundf(20 * term->scale);
}
