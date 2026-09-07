#include "tab.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define LOG_MODULE "tab"
#define LOG_ENABLE_DBG 0
#include "log.h"

#include "config.h"
#include "fdm.h"
#include "macros.h"
#include "render.h"
#include "shm.h"
#include "tab-activity.h"
#include "tab-close.h"
#include "terminal.h"
#include "util.h"
#include "vt.h"
#include "wayland.h"
#include "xmalloc.h"

/* Debounce for the /proc lookups driven from the PTY read path */
#define PROC_CHECK_MS 250

static void split_layout(struct wl_window *win);

void
tab_bar_init(struct tab_bar *tb)
{
    *tb = (struct tab_bar){
        .tabs = tll_init(),
        .hovered_tab = -1,
        .split_hovered = -1,
        .dirty = true,
        .pulse_timer_fd = -1,
    };
}

static void
pane_unmap(struct tab *tab)
{
    if (tab->pane != NULL) {
        wl_surface_attach(tab->pane->surface.surf, NULL, 0, 0);
        wl_surface_commit(tab->pane->surface.surf);
    }
}

static void
pane_destroy(struct tab *tab)
{
    if (tab->pane_frame_cb != NULL) {
        wl_callback_destroy(tab->pane_frame_cb);
        tab->pane_frame_cb = NULL;
    }
    if (tab->pane != NULL) {
        pane_unmap(tab);
        wayl_win_subsurface_destroy(tab->pane);
        free(tab->pane);
        tab->pane = NULL;
    }
}

void
tab_bar_unmap(struct tab_bar *tb)
{
    if (tb->surface != NULL) {
        wl_surface_attach(tb->surface->surface.surf, NULL, 0, 0);
        wl_surface_commit(tb->surface->surface.surf);
    }
    tll_foreach(tb->tabs, it)
        pane_unmap(&it->item);
}

void
tab_bar_destroy(struct tab_bar *tb, struct fdm *fdm)
{
    tll_foreach(tb->tabs, it) {
        pane_destroy(&it->item);
        free(it->item.title);
        tll_remove(tb->tabs, it);
    }

    fcft_destroy(tb->font);
    tb->font = NULL;

    if (tb->surface != NULL) {
        wayl_win_subsurface_destroy(tb->surface);
        free(tb->surface);
        tb->surface = NULL;
    }

    if (tb->chain != NULL) {
        shm_chain_free(tb->chain);
        tb->chain = NULL;
    }

    if (tb->pulse_timer_fd >= 0) {
        fdm_del(fdm, tb->pulse_timer_fd);
        tb->pulse_timer_fd = -1;
    }

    free(tb->tab_x_ends);
    tb->tab_x_ends = NULL;
    tb->active = NULL;
    tb->tab_count = 0;
    tb->split_mode = false;
}

static char *
title_from_cwd(struct terminal *term)
{
    char cwd_buf[PATH_MAX];
    const char *path = term_shell_cwd(term, cwd_buf, sizeof(cwd_buf));
    if (path == NULL)
        return xstrdup("shell");

    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0' &&
        strncmp(path, home, strlen(home)) == 0)
    {
        const char *rest = path + strlen(home);
        if (*rest == '\0')
            return xstrdup("~");
        if (*rest == '/')
            return xasprintf("~%s", rest);
    }
    return xstrdup(path);
}

static struct tab *
find_tab(const struct wl_window *win, const struct terminal *term)
{
    if (win == NULL)
        return NULL;
    tll_foreach(win->tab_bar.tabs, it) {
        if (it->item.term == term)
            return &it->item;
    }
    return NULL;
}

static struct tab *
tab_at_index(struct wl_window *win, int index)
{
    int i = 0;
    tll_foreach(win->tab_bar.tabs, it) {
        if (i++ == index)
            return &it->item;
    }
    return NULL;
}

/* Returns true if the title changed */
static bool
refresh_title(struct tab *tab)
{
    clock_gettime(CLOCK_MONOTONIC, &tab->last_title_check);

    char *new_title = title_from_cwd(tab->term);
    if (tab->title != NULL && strcmp(tab->title, new_title) == 0) {
        free(new_title);
        return false;
    }

    free(tab->title);
    tab->title = new_title;
    return true;
}

static void
push_tab(struct tab_bar *tb, struct terminal *term)
{
    tll_push_back(tb->tabs, ((struct tab){.term = term}));
    refresh_title(&tll_back(tb->tabs));
    tb->tab_count++;
    tb->dirty = true;
}

void
tab_bar_add_initial(struct tab_bar *tb, struct terminal *term)
{
    push_tab(tb, term);
    tb->active = &tll_back(tb->tabs);
}

/* Zoom is per terminal; make dst render at the same size as src */
static void
copy_font_state(struct terminal *dst, const struct terminal *src)
{
    if (dst->cell_width == src->cell_width &&
        dst->cell_height == src->cell_height)
    {
        return;
    }

    for (size_t i = 0; i < 4; i++) {
        const size_t count = min(
            dst->conf->fonts[i].count, src->conf->fonts[i].count);
        for (size_t j = 0; j < count; j++)
            dst->font_sizes[i][j] = src->font_sizes[i][j];

        fcft_destroy(dst->fonts[i]);
        dst->fonts[i] = src->fonts[i] != NULL
            ? fcft_clone(src->fonts[i]) : NULL;
    }
    dst->cell_width = src->cell_width;
    dst->cell_height = src->cell_height;
    dst->font_x_ofs = src->font_x_ofs;
    dst->font_y_ofs = src->font_y_ofs;
    dst->font_baseline = src->font_baseline;
    dst->font_line_height = src->font_line_height;
}

static void
do_tab_switch(struct wl_window *win, struct tab *new_tab)
{
    if (win->tab_bar.active == new_tab)
        return;

    struct terminal *old_term = win->tab_bar.active->term;
    struct terminal *new_term = new_tab->term;

    if (!win->tab_bar.split_mode) {
        /*
         * The old tab must not draw into the shared surface anymore.
         * Drop its queued work and any in-flight frame callback:
         * frame_callback() only services its own terminal, so leaving
         * the callback in place would strand the new tab's render
         * until its next refresh.
         */
        old_term->render.refresh.grid = false;
        old_term->render.pending.grid = false;
        old_term->render.pending.csd = false;
        old_term->render.pending.search = false;
        old_term->render.pending.urls = false;
        if (win->frame_callback != NULL) {
            wl_callback_destroy(win->frame_callback);
            win->frame_callback = NULL;
        }
    }

    win->tab_bar.active = new_tab;
    win->term = new_term;

    /* Copy active_surface from old terminal so pointer state is consistent */
    new_term->active_surface = old_term->active_surface;

    copy_font_state(new_term, old_term);

    /* Sync dimensions: resize new tab to match current window (skip in split mode) */
    new_term->scale = old_term->scale;
    if (!win->tab_bar.split_mode) {
        int logical_width = (int)roundf(old_term->width / old_term->scale);
        int logical_height = (int)roundf(old_term->height / old_term->scale);
        render_resize(new_term, logical_width, logical_height, RESIZE_FORCE);
    }

    /* Transfer keyboard focus to new terminal */
    bool had_focus = old_term->kbd_focus;
    tll_foreach(new_term->wl->seats, it) {
        if (it->item.kbd_focus == old_term)
            it->item.kbd_focus = new_term;
        if (it->item.mouse_focus == old_term)
            it->item.mouse_focus = new_term;
        if (it->item.ime_focus == old_term)
            it->item.ime_focus = new_term;
    }
    if (had_focus) {
        old_term->kbd_focus = false;
        new_term->kbd_focus = true;
    }

    /* Trigger full redraw of the new tab */
    term_damage_all(new_term);
    render_refresh(new_term);

    /* In split mode, also redraw old pane to update dim state */
    if (win->tab_bar.split_mode) {
        term_damage_all(old_term);
        render_refresh(old_term);
    }

    win->tab_bar.dirty = true;

    if (new_term->window_title != NULL)
        xdg_toplevel_set_title(win->xdg_toplevel, new_term->window_title);

    LOG_DBG("switched to tab %d", tab_index_of(win, new_term));
}

static void
resize_to_window(struct terminal *term)
{
    if (term->width <= 0)
        return;
    render_resize(term,
                  (int)roundf(term->width / term->scale),
                  (int)roundf(term->height / term->scale),
                  RESIZE_FORCE);
}

void
tab_attach(struct wl_window *win, struct terminal *new_term)
{
    struct wayland *wayl = new_term->wl;
    struct tab_bar *tb = &win->tab_bar;

    /* Exit split mode before adding a new tab */
    if (tb->split_mode)
        tab_split_exit(win);

    push_tab(tb, new_term);

    if (tb->surface == NULL) {
        tb->surface = xcalloc(1, sizeof(*tb->surface));
        if (!wayl_win_subsurface_new(win, tb->surface, true)) {
            LOG_ERR("failed to create tab bar subsurface");
            free(tb->surface);
            tb->surface = NULL;
        }
    }

    if (tb->chain == NULL)
        tb->chain = shm_chain_new(wayl, false, 1, SHM_BITS_8, NULL, NULL);

    /*
     * The new terminal shares the existing (already configured) window,
     * but has no grid yet. We need to:
     * 1. Enable its PTY FDM callback (term_window_configured does this)
     * 2. Resize it to match the current window size (allocates the grid)
     */
    term_window_configured(new_term);

    struct terminal *reference = tb->active->term;
    render_resize(new_term,
                  (int)roundf(reference->width / reference->scale),
                  (int)roundf(reference->height / reference->scale),
                  RESIZE_FORCE);

    /*
     * The bar appears with the second tab and takes its height from
     * every tab's grid. Later tabs leave the bar as it is. Titles of
     * tabs that were alone may be stale: title refresh is only driven
     * while the bar is shown.
     */
    if (tb->tab_count == 2) {
        tll_foreach(tb->tabs, it) {
            if (it->item.term != new_term) {
                resize_to_window(it->item.term);
                refresh_title(&it->item);
            }
        }
    }

    do_tab_switch(win, &tll_back(tb->tabs));

    LOG_INFO("new tab attached (total: %d)", tb->tab_count);
}

bool
tab_new(struct terminal *term)
{
    struct wl_window *win = term->window;

    char cwd_buf[PATH_MAX];
    const char *cwd = term_shell_cwd(term, cwd_buf, sizeof(cwd_buf));

    struct terminal *new_term = term_init(
        term->conf, term->fdm, term->reaper, term->wl,
        term->foot_exe, cwd,
        NULL,  /* token */
        NULL,  /* pty_path */
        0, NULL, NULL,  /* argc, argv, envp - use defaults from conf */
        term->shutdown.cb, term->shutdown.cb_data,
        win);  /* reuse existing window */

    if (new_term == NULL) {
        LOG_ERR("failed to create new terminal for tab");
        return false;
    }

    tab_attach(win, new_term);
    return true;
}

void
tab_detach(struct terminal *term)
{
    struct wl_window *win = term->window;
    struct tab_bar *tb = &win->tab_bar;
    struct tab *closing = find_tab(win, term);

    xassert(closing != NULL);
    xassert(tb->tab_count > 1);

    int closing_index = tab_index_of(win, term);
    int active_index = tab_index_of(win, tb->active->term);
    int focus_target = tab_close_focus_target(
        tb->tab_count, closing_index, active_index);
    xassert(focus_target >= 0);

    /* Move focus away before the tab disappears (indices are pre-removal) */
    if (focus_target != active_index)
        tab_switch_to(win, focus_target);

    pane_destroy(closing);

    tll_foreach(tb->tabs, it) {
        if (&it->item == closing) {
            free(it->item.title);
            tll_remove(tb->tabs, it);
            break;
        }
    }

    tb->tab_count--;
    tb->dirty = true;
    tb->split_hovered = -1;
    term->window = NULL;

    if (tb->split_mode) {
        if (tb->tab_count <= 1)
            tab_split_exit(win);
        else
            split_layout(win);
    }

    /* Hide the bar when down to one tab, and give the grid its space back */
    if (tb->tab_count <= 1 && tb->surface != NULL) {
        wl_surface_attach(tb->surface->surface.surf, NULL, 0, 0);
        wl_surface_commit(tb->surface->surface.surf);

        struct terminal *active = tb->active->term;
        resize_to_window(active);
        term_damage_all(active);
        render_refresh(active);
    }

    LOG_INFO("tab detached (remaining: %d)", tb->tab_count);
}

void
tab_shutdown_window(struct wl_window *win)
{
    struct tab_bar *tb = &win->tab_bar;
    struct terminal *active = tb->active != NULL ? tb->active->term : win->term;

    /*
     * term_shutdown() detaches every tab but the last, mutating the
     * list, so work from a snapshot. Inactive tabs go first so focus
     * never has to move; the active tab, last, takes the window down.
     */
    size_t count = 0;
    struct terminal **terms = xmalloc(
        (tb->tab_count > 0 ? tb->tab_count : 1) * sizeof(terms[0]));
    tll_foreach(tb->tabs, it) {
        if (it->item.term != active)
            terms[count++] = it->item.term;
    }

    for (size_t i = 0; i < count; i++)
        term_shutdown(terms[i]);
    term_shutdown(active);

    free(terms);
}

static bool
tab_close_internal(struct wl_window *win, struct tab *closing)
{
    if (win->tab_bar.tab_count <= 1)
        return false;  /* Last tab - caller should close window */

    /* term_shutdown() detaches the tab before its deferred teardown runs */
    if (!term_shutdown(closing->term))
        LOG_ERR("failed to shut down closed tab");
    return true;
}

bool
tab_close_active(struct terminal *term)
{
    return tab_close_internal(term->window, term->window->tab_bar.active);
}

bool
tab_close_at_index(struct wl_window *win, int index)
{
    struct tab *tab = tab_at_index(win, index);
    return tab != NULL && tab_close_internal(win, tab);
}

void
tab_ctx_menu_show(struct terminal *term, int target_tab, int x, int y)
{
    struct tab_bar *tb = &term->window->tab_bar;
    if (target_tab < 0 || target_tab >= tb->tab_count)
        return;

    tb->ctx_menu_visible = true;
    tb->ctx_menu_target_tab = target_tab;
    tb->ctx_menu_x = x;
    tb->ctx_menu_y = y;
    tb->ctx_menu_hovered_item = -1;
    tb->ctx_menu_item_count = 2;  /* Close Tab, Duplicate Tab */
    tb->ctx_menu_w = 0;           /* recomputed at render time */
    tb->ctx_menu_h = 0;
    render_refresh(term);
}

void
tab_ctx_menu_dismiss(struct terminal *term)
{
    struct wl_window *win = term->window;
    struct tab_bar *tb = &win->tab_bar;
    if (!tb->ctx_menu_visible)
        return;
    tb->ctx_menu_visible = false;
    tb->ctx_menu_hovered_item = -1;

    /*
     * Eagerly unmap the overlay subsurface so the menu disappears even
     * when the next render targets a different term. This matters for
     * the action items: "Close Tab" and "Duplicate Tab" both switch
     * focus, and the new active term's `render.last_overlay_style` is
     * usually OVERLAY_NONE, so its render_overlay() wouldn't know to
     * unmap. Without this, stale menu pixels remain on the overlay.
     */
    if (win->overlay.surface.surf != NULL) {
        wl_surface_attach(win->overlay.surface.surf, NULL, 0, 0);
        wl_surface_commit(win->overlay.surface.surf);
        term->render.last_overlay_style = OVERLAY_NONE;
        term->render.last_overlay_buf = NULL;
    }

    render_refresh(term);
}

static int
ctx_menu_item_at(const struct tab_bar *tb, int x, int y)
{
    if (tb->ctx_menu_w <= 0 || tb->ctx_menu_h <= 0 ||
        x < tb->ctx_menu_x || x >= tb->ctx_menu_x + tb->ctx_menu_w ||
        y < tb->ctx_menu_y || y >= tb->ctx_menu_y + tb->ctx_menu_h)
    {
        return -1;
    }

    int item_h = tb->ctx_menu_h / tb->ctx_menu_item_count;
    int item = item_h > 0 ? (y - tb->ctx_menu_y) / item_h : 0;
    return min(item, tb->ctx_menu_item_count - 1);
}

bool
tab_ctx_menu_update_hover(struct terminal *term, int x, int y)
{
    struct tab_bar *tb = &term->window->tab_bar;
    if (!tb->ctx_menu_visible)
        return false;

    int hovered = ctx_menu_item_at(tb, x, y);
    if (hovered == tb->ctx_menu_hovered_item)
        return false;

    tb->ctx_menu_hovered_item = hovered;
    render_refresh(term);
    return true;
}

bool
tab_ctx_menu_handle_click(struct terminal *term, int x, int y)
{
    struct wl_window *win = term->window;
    struct tab_bar *tb = &win->tab_bar;
    if (!tb->ctx_menu_visible)
        return false;

    int item = ctx_menu_item_at(tb, x, y);
    int target_tab = tb->ctx_menu_target_tab;

    /* Resolve the target before dismissing/closing: indices may shift */
    struct tab *target = tab_at_index(win, target_tab);
    struct terminal *target_term = target != NULL ? target->term : NULL;

    tab_ctx_menu_dismiss(term);

    switch (item) {
    case -1:  /* Click outside the menu: dismiss without action */
        break;

    case 0:  /* Close Tab */
        tab_close_at_index(win, target_tab);
        break;

    case 1:  /* Duplicate Tab */
        if (target_term != NULL)
            tab_new(target_term);
        break;
    }

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
    struct tab *tab = tab_at_index(win, index);
    if (tab != NULL)
        do_tab_switch(win, tab);
}

void
tab_refresh_title(struct terminal *term)
{
    struct tab *tab = find_tab(term->window, term);
    if (tab != NULL && refresh_title(tab))
        term->window->tab_bar.dirty = true;
}

/*
 * Lay the panes out in pre_split_lw x pre_split_lh logical pixels. The
 * last column and row absorb the integer-division remainder so the panes
 * cover the whole window.
 */
static void
split_layout(struct wl_window *win)
{
    struct tab_bar *tb = &win->tab_bar;
    const int count = tb->tab_count;

    int cols, rows;
    if (count <= 3) {
        /* 2-3 panes: side by side columns, full height each */
        cols = count;
        rows = 1;
    } else {
        cols = (int)ceilf(sqrtf((float)count));
        rows = (count + cols - 1) / cols;
    }

    tb->split_cols = cols;
    tb->split_rows = rows;
    tb->split_hovered = -1;

    const int pane_lw = tb->pre_split_lw / cols;
    const int pane_lh = tb->pre_split_lh / rows;
    const int rem_w = tb->pre_split_lw - pane_lw * cols;
    const int rem_h = tb->pre_split_lh - pane_lh * rows;

    LOG_DBG("split layout: %d tabs, %dx%d grid, pane=%dx%d logical, total=%dx%d",
            count, cols, rows, pane_lw, pane_lh,
            tb->pre_split_lw, tb->pre_split_lh);

    int idx = 0;
    tll_foreach(tb->tabs, it) {
        struct tab *tab = &it->item;
        tab->pane_col = idx % cols;
        tab->pane_row = idx / cols;
        idx++;

        if (tab->pane != NULL) {
            wl_subsurface_set_position(
                tab->pane->sub,
                tab->pane_col * pane_lw, tab->pane_row * pane_lh);
        }

        const int lw = pane_lw + (tab->pane_col == cols - 1 ? rem_w : 0);
        const int lh = pane_lh + (tab->pane_row == rows - 1 ? rem_h : 0);
        render_resize(tab->term, lw, lh, RESIZE_FORCE);
    }

    /*
     * Subsurface positions take effect on the parent's next commit. The
     * commit also acks a configure that arrived while in split mode.
     */
    wl_surface_commit(win->surface.surf);

    tll_foreach(tb->tabs, it) {
        term_damage_margins(it->item.term);
        term_damage_all(it->item.term);
        render_refresh(it->item.term);
    }
}

void
tab_split_enter(struct wl_window *win)
{
    struct tab_bar *tb = &win->tab_bar;
    if (tb->tab_count <= 1 || tb->split_mode)
        return;

    struct terminal *active = tb->active->term;

    /* Lay the panes out in the current content area */
    tb->pre_split_lw = (int)roundf(active->width / active->scale);
    tb->pre_split_lh = (int)roundf(active->height / active->scale);
    tb->split_mode = true;

    /* Hide the tab bar subsurface */
    if (tb->surface != NULL) {
        wl_surface_attach(tb->surface->surface.surf, NULL, 0, 0);
        wl_surface_commit(tb->surface->surface.surf);
    }

    tll_foreach(tb->tabs, it) {
        struct tab *tab = &it->item;

        copy_font_state(tab->term, active);

        tab->pane = xcalloc(1, sizeof(*tab->pane));
        if (!wayl_win_subsurface_new(win, tab->pane, true)) {
            LOG_ERR("failed to create pane subsurface");
            free(tab->pane);
            tab->pane = NULL;
            continue;
        }

        /* Desync so each pane can commit independently */
        wl_subsurface_set_desync(tab->pane->sub);
    }

    split_layout(win);
}

void
tab_split_exit(struct wl_window *win)
{
    struct tab_bar *tb = &win->tab_bar;
    if (!tb->split_mode)
        return;

    tb->split_mode = false;
    tb->split_hovered = -1;

    tll_foreach(tb->tabs, it)
        pane_destroy(&it->item);

    /* Restore all terminals to the full content area */
    tll_foreach(tb->tabs, it) {
        render_resize(it->item.term, tb->pre_split_lw, tb->pre_split_lh,
                      RESIZE_FORCE);
    }

    struct terminal *active = tb->active->term;
    term_damage_all(active);
    render_refresh(active);
    tb->dirty = true;
}

void
tab_split_resize(struct wl_window *win, int logical_width, int logical_height)
{
    struct tab_bar *tb = &win->tab_bar;
    if (!tb->split_mode)
        return;

    struct terminal *active = tb->active->term;

    if (logical_width <= 0 || logical_height <= 0) {
        /* The compositor leaves the size to us (e.g. un-maximize): back
         * to the last floating size, which pane resizes never overwrite */
        if (active->stashed_width <= 0 || active->stashed_height <= 0)
            return;
        logical_width = (int)roundf(active->stashed_width / active->scale);
        logical_height = (int)roundf(active->stashed_height / active->scale);
    }

    if (logical_width == tb->pre_split_lw && logical_height == tb->pre_split_lh)
        return;

    tb->pre_split_lw = logical_width;
    tb->pre_split_lh = logical_height;
    split_layout(win);

    /* Panes do not touch the window geometry; keep it at the full size */
    render_set_window_geometry(
        active,
        (int)roundf(logical_width * active->scale),
        (int)roundf(logical_height * active->scale));
}

void
tab_split_focus(struct wl_window *win, int index)
{
    if (!win->tab_bar.split_mode)
        return;
    tab_switch_to(win, index);
}

bool
tab_split_active_pane_origin(const struct wl_window *win, int *x, int *y)
{
    const struct tab_bar *tb = &win->tab_bar;
    *x = *y = 0;

    if (!tb->split_mode || tb->active == NULL || tb->active->pane == NULL)
        return false;

    *x = tb->active->pane_col * (tb->pre_split_lw / tb->split_cols);
    *y = tb->active->pane_row * (tb->pre_split_lh / tb->split_rows);
    return true;
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

struct wl_callback **
tab_pane_frame_cb(struct wl_window *win, struct terminal *term)
{
    struct tab *tab = find_tab(win, term);
    return tab != NULL ? &tab->pane_frame_cb : NULL;
}

struct wl_surface *
tab_topmost_surface(const struct wl_window *win)
{
    const struct tab_bar *tb = &win->tab_bar;

    if (tb->split_mode) {
        /* Panes are created in list order; the last one stacks on top */
        tll_rforeach(tb->tabs, it) {
            if (it->item.pane != NULL)
                return it->item.pane->surface.surf;
        }
    }

    return tb->surface != NULL ? tb->surface->surface.surf : NULL;
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
    if (term->window->tab_bar.split_mode)
        return 0;

    /* Fixed tab bar height in pixels, scaled for monitor */
    return (int)roundf(20 * term->scale);
}

static int64_t
ms_since(const struct timespec *t)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t ds = now.tv_sec - t->tv_sec;
    int64_t dn = now.tv_nsec - t->tv_nsec;
    return ds * 1000 + dn / 1000000;
}

/* Update the tab's cached foreground-process classification from /proc. */
static bool
refresh_fg_activity_match(struct tab *tab)
{
    struct terminal *term = tab->term;
    const struct config *conf = term->conf;

    clock_gettime(CLOCK_MONOTONIC, &tab->last_fg_check);

    if (!conf->tab_bar.activity_pulse ||
        conf->tab_bar.activity_pulse_processes == NULL ||
        conf->tab_bar.activity_pulse_processes[0] == '\0')
    {
        tab->fg_activity_match = false;
        return false;
    }

    pid_t fg_pgid = term_foreground_pgid(term);
    if (fg_pgid <= 0) {
        tab->cached_fg_pgid = fg_pgid;
        tab->fg_activity_match = false;
        return false;
    }

    /* Same pgid as last check - reuse cached classification */
    if (fg_pgid == tab->cached_fg_pgid)
        return tab->fg_activity_match;

    char comm[256];
    tab->cached_fg_pgid = fg_pgid;
    tab->fg_activity_match =
        term_process_comm(fg_pgid, comm, sizeof(comm)) &&
        tab_activity_process_matches(
            conf->tab_bar.activity_pulse_processes, comm);
    return tab->fg_activity_match;
}

static bool fdm_pulse_timer(struct fdm *fdm, int fd, int events, void *data);

static void
pulse_timer_arm(struct wl_window *win)
{
    struct tab_bar *tb = &win->tab_bar;
    if (tb->pulse_timer_fd >= 0)
        return;

    struct fdm *fdm = win->term->fdm;

    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (fd < 0) {
        LOG_ERRNO("failed to create tab pulse timer");
        return;
    }
    if (!fdm_add(fdm, fd, EPOLLIN, &fdm_pulse_timer, win)) {
        close(fd);
        return;
    }

    /* ~50ms ticks - smooth pulsation without burning CPU */
    const struct itimerspec timer = {
        .it_value =    {.tv_sec = 0, .tv_nsec = 50 * 1000000},
        .it_interval = {.tv_sec = 0, .tv_nsec = 50 * 1000000},
    };
    if (timerfd_settime(fd, 0, &timer, NULL) < 0) {
        LOG_ERRNO("failed to arm tab pulse timer");
        fdm_del(fdm, fd);
        return;
    }
    tb->pulse_timer_fd = fd;
}

static bool
fdm_pulse_timer(struct fdm *fdm, int fd, int events, void *data)
{
    struct wl_window *win = data;
    struct tab_bar *tb = &win->tab_bar;

    uint64_t expirations;
    ssize_t r = read(fd, &expirations, sizeof(expirations));
    (void)r;

    /* Re-check activity and disarm when no configured process is active. */
    bool any_activity = false;
    tll_foreach(tb->tabs, it) {
        if (tab_activity_is_active(&it->item))
            any_activity = true;
    }

    /* Only the bar changes: the render hook picks the dirty flag up on
     * its own, no grid render needed */
    tb->dirty = true;

    if (!any_activity) {
        fdm_del(fdm, fd);
        tb->pulse_timer_fd = -1;
    }

    return true;
}

void
tab_on_output(struct terminal *term)
{
    struct tab *tab = find_tab(term->window, term);
    if (tab == NULL)
        return;

    struct tab_bar *tb = &term->window->tab_bar;

    /* A cwd change always comes with output (the new prompt) */
    if (ms_since(&tab->last_title_check) >= PROC_CHECK_MS &&
        refresh_title(tab))
    {
        tb->dirty = true;
    }

    if (ms_since(&tab->last_fg_check) >= PROC_CHECK_MS)
        refresh_fg_activity_match(tab);

    if (tab->fg_activity_match && tb->pulse_timer_fd < 0) {
        tb->dirty = true;
        pulse_timer_arm(term->window);
    }
}

bool
tab_activity_is_active(struct tab *tab)
{
    struct terminal *term = tab->term;
    if (term == NULL || term->ptmx < 0)
        return false;

    if (ms_since(&tab->last_fg_check) >= PROC_CHECK_MS)
        refresh_fg_activity_match(tab);

    if (!tab->fg_activity_match)
        return false;

    return ms_since(&term->last_pty_activity) <
        term->conf->tab_bar.activity_pulse_quiet_ms;
}
