#pragma once

#include <stdbool.h>
#include <time.h>
#include <sys/types.h>
#include <tllist.h>

struct terminal;
struct wl_window;
struct wl_callback;
struct wl_surface;
struct fdm;
struct wayl_sub_surface;
struct buffer_chain;

struct tab {
    struct terminal *term;
    char *title;
    struct wayl_sub_surface *pane;      /* split mode pane surface, NULL in tab mode */
    struct wl_callback *pane_frame_cb;  /* per-pane frame callback in split mode */
    int pane_col;                       /* column in split grid */
    int pane_row;                       /* row in split grid */

    /* Debounced /proc lookups, driven from the PTY read path */
    struct timespec last_title_check;
    struct timespec last_fg_check;
    pid_t cached_fg_pgid;
    bool fg_activity_match;
};

typedef tll(struct tab) tab_list_t;

struct tab_bar {
    tab_list_t tabs;
    struct tab *active;
    struct wayl_sub_surface *surface;
    struct buffer_chain *chain;

    /* Regular font at the configured size (unaffected by zoom), loaded
     * lazily for the DPI/scale recorded alongside it */
    struct fcft_font *font;
    float font_dpi;
    float font_scale;
    bool font_sized_by_dpi;

    int tab_count;
    int hovered_tab;             /* index of tab under mouse, -1 if none */
    bool split_mode;             /* true when split pane mode is active */
    int split_hovered;           /* index of pane under mouse, -1 if none */
    int pre_split_lw;            /* logical width the panes are laid out in */
    int pre_split_lh;            /* logical height the panes are laid out in */
    int split_cols;              /* number of columns in split grid */
    int split_rows;              /* number of rows in split grid */
    int *tab_x_ends;             /* cumulative x end positions for hit-testing */
    bool dirty;

    /* Activity pulse timer (-1 when idle) */
    int pulse_timer_fd;

    /* Right-click context menu */
    bool ctx_menu_visible;
    int ctx_menu_target_tab;     /* tab index the menu was opened on */
    int ctx_menu_x, ctx_menu_y;  /* top-left anchor in window pixels */
    int ctx_menu_w, ctx_menu_h;  /* size, computed at render time */
    int ctx_menu_hovered_item;   /* 0..ctx_menu_item_count-1, or -1 */
    int ctx_menu_item_count;     /* number of items in the menu */
};

void tab_bar_init(struct tab_bar *tb);

/* Unmap the tab bar and pane surfaces (part of tearing down the window). */
void tab_bar_unmap(struct tab_bar *tb);

/* Release everything the tab bar owns: tab list, pane surfaces, font,
 * buffer chain, pulse timer. Called from wayl_win_destroy(). */
void tab_bar_destroy(struct tab_bar *tb, struct fdm *fdm);

/* Add the initial terminal as the first tab */
void tab_bar_add_initial(struct tab_bar *tb, struct terminal *term);

/* Create a new tab in the same window */
bool tab_new(struct terminal *term);

/* Attach an already-created terminal to a window as a new tab. The terminal
 * must have been created via term_init() with `existing_window` set to `win`.
 * Handles tab list insertion, tab bar subsurface/chain creation, sizing of
 * the new and existing tabs, and switching focus to the new tab. */
void tab_attach(struct wl_window *win, struct terminal *new_term);

/* Remove a terminal from its window without shutting it down: moves focus
 * to a neighbor, releases its pane, re-lays out split mode, hides the bar
 * when one tab remains, and clears term->window. Called by term_shutdown()
 * for any terminal that shares its window with other tabs. Requires more
 * than one tab. */
void tab_detach(struct terminal *term);

/* Shut down every tab in the window. The last one tears the window down. */
void tab_shutdown_window(struct wl_window *win);

/* Close the active tab. Returns false if it was the last tab. */
bool tab_close_active(struct terminal *term);

/* Close a specific tab by index. Returns false if the index is invalid or
 * if it was the last tab. */
bool tab_close_at_index(struct wl_window *win, int index);

/* Right-click context menu on the tab bar. */
void tab_ctx_menu_show(struct terminal *term, int target_tab, int x, int y);
void tab_ctx_menu_dismiss(struct terminal *term);
/* Returns true if the click hit the menu (action taken or dismissed). */
bool tab_ctx_menu_handle_click(struct terminal *term, int x, int y);
/* Updates hovered item based on pointer position. Returns true if state
 * changed and a re-render is needed. */
bool tab_ctx_menu_update_hover(struct terminal *term, int x, int y);

/* Switch to the next tab. Wraps around. */
void tab_next(struct terminal *term);

/* Switch to the previous tab. Wraps around. */
void tab_prev(struct terminal *term);

/* Switch to a specific tab by index (0-based). */
void tab_switch_to(struct wl_window *win, int index);

/* Re-read the terminal's tab title from the shell's cwd. */
void tab_refresh_title(struct terminal *term);

/* Get the tab index for a given terminal. Returns -1 if not found. */
int tab_index_of(const struct wl_window *win, const struct terminal *term);

/* Get the number of tabs. */
int tab_count(const struct wl_window *win);

/* Enter split pane mode - show all tabs as live panes. */
void tab_split_enter(struct wl_window *win);

/* Exit split pane mode - return to tabbed view. */
void tab_split_exit(struct wl_window *win);

/* Re-lay out the panes for a new window size (logical pixels). */
void tab_split_resize(struct wl_window *win, int logical_width,
                      int logical_height);

/* Switch focus to a specific pane by index in split mode. */
void tab_split_focus(struct wl_window *win, int index);

/* Logical position of the active pane in split mode. Returns false (and
 * zeroes) when not in split mode. */
bool tab_split_active_pane_origin(const struct wl_window *win, int *x, int *y);

/* Get per-pane frame callback pointer for a terminal in split mode. */
struct wl_callback **tab_pane_frame_cb(struct wl_window *win,
                                       struct terminal *term);

/* The topmost tab-owned subsurface (a pane in split mode, else the tab
 * bar), or NULL. Overlays are stacked above it. */
struct wl_surface *tab_topmost_surface(const struct wl_window *win);

/* Get the tab bar height in pixels (0 if hidden). */
int tab_bar_height(const struct terminal *term);

/* Called from the PTY read path when a terminal produces output. Refreshes
 * the tab title and the foreground-process classification (both debounced)
 * and arms the tab-bar pulse timer when a configured process is active. */
void tab_on_output(struct terminal *term);

/* Returns true if the tab's foreground process is configured for activity
 * indication and there has been recent PTY output. Updates the cached
 * foreground-process classification as a side effect. */
bool tab_activity_is_active(struct tab *tab);
