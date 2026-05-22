#pragma once

#include <stdbool.h>
#include <time.h>
#include <sys/types.h>
#include <tllist.h>

struct terminal;
struct wl_window;
struct wl_callback;
struct fdm;
struct reaper;
struct wayland;
struct config;
struct wayl_sub_surface;
struct buffer_chain;

struct tab {
    struct terminal *term;
    char *title;
    bool urgent;
    struct wayl_sub_surface *pane;      /* split mode pane surface, NULL in tab mode */
    struct wl_callback *pane_frame_cb;  /* per-pane frame callback in split mode */
    int pane_col;                       /* column in split grid */
    int pane_row;                       /* row in split grid */

    /* Cached foreground process info for claude pulsate effect */
    pid_t cached_fg_pgid;
    bool fg_is_claude;
    struct timespec last_fg_check;
};

struct closed_tab {
    struct terminal *term;
    char *title;
    int timer_fd;
    char *scrollback;
    size_t scrollback_len;
    char *cwd;
};

typedef tll(struct tab) tab_list_t;
typedef tll(struct closed_tab) closed_tab_list_t;

struct tab_bar {
    tab_list_t tabs;
    struct tab *active;
    closed_tab_list_t closed;
    struct wayl_sub_surface *surface;
    struct buffer_chain *chain;
    struct fcft_font *font;      /* fixed font for tab bar, not affected by zoom */
    int height;                  /* fixed height in pixels, set on first tab creation */
    int tab_count;
    int undo_timeout_ms;
    int hovered_tab;             /* index of tab under mouse, -1 if none */
    bool split_mode;             /* true when split pane mode is active */
    int split_hovered;           /* index of pane under mouse, -1 if none */
    int pre_split_lw;            /* saved logical width before split */
    int pre_split_lh;            /* saved logical height before split */
    int split_cols;              /* number of columns in split grid */
    int split_rows;              /* number of rows in split grid */
    int *tab_x_ends;             /* cumulative x end positions for hit-testing */
    bool dirty;

    /* Pulsate timer for claude-working tabs (-1 when idle) */
    int pulse_timer_fd;

    /* Right-click context menu */
    bool ctx_menu_visible;
    int ctx_menu_target_tab;     /* tab index the menu was opened on */
    int ctx_menu_x, ctx_menu_y;  /* top-left anchor in window pixels */
    int ctx_menu_w, ctx_menu_h;  /* size, computed at render time */
    int ctx_menu_hovered_item;   /* 0..ctx_menu_item_count-1, or -1 */
    int ctx_menu_item_count;     /* number of items in the menu */
};

void tab_bar_init(struct tab_bar *tb, int undo_timeout_ms);
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

/* Undo the last closed tab. Returns true if a tab was restored. */
bool tab_undo_close(struct terminal *term);

/* Update the title for the tab containing the given terminal. */
void tab_update_title(struct wl_window *win, struct terminal *term,
                      const char *title);

/* Get the tab index for a given terminal. Returns -1 if not found. */
int tab_index_of(const struct wl_window *win, const struct terminal *term);

/* Get the number of tabs. */
int tab_count(const struct wl_window *win);

/* Refresh all tab titles from /proc/<pid>/cwd. */
void tab_bar_refresh_titles(struct wl_window *win, struct terminal *term);

/* Enter split pane mode - show all tabs as live panes. */
void tab_split_enter(struct wl_window *win);

/* Exit split pane mode - return to tabbed view. */
void tab_split_exit(struct wl_window *win);

/* Switch focus to a specific pane by index in split mode. */
void tab_split_focus(struct wl_window *win, int index);

/* Get per-pane frame callback pointer for a terminal in split mode. */
struct wl_callback **tab_pane_frame_cb(struct wl_window *win,
                                       struct terminal *term);

/* Get the tab bar height in pixels (0 if hidden). */
int tab_bar_height(const struct terminal *term);

/* Called from PTY read path when a terminal produces output. Re-checks
 * (with debounce) whether the terminal's foreground process is `claude`,
 * and arms the tab-bar pulse timer if so. Cheap when not-claude. */
void tab_pulse_kick(struct terminal *term);

/* Returns true if the tab's foreground process is `claude` and there has
 * been recent PTY output (i.e. claude is actively working). Updates the
 * cached foreground-process info on the tab as a side effect. */
bool tab_is_working(struct tab *tab);
