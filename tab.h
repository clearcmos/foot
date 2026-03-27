#pragma once

#include <stdbool.h>
#include <tllist.h>

struct terminal;
struct wl_window;
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
    int *tab_x_ends;             /* cumulative x end positions for hit-testing */
    bool dirty;
};

void tab_bar_init(struct tab_bar *tb, int undo_timeout_ms);
void tab_bar_destroy(struct tab_bar *tb, struct fdm *fdm);

/* Add the initial terminal as the first tab */
void tab_bar_add_initial(struct tab_bar *tb, struct terminal *term);

/* Create a new tab in the same window */
bool tab_new(struct terminal *term);

/* Close the active tab. Returns false if it was the last tab. */
bool tab_close_active(struct terminal *term);

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

/* Get the tab bar height in pixels (0 if hidden). */
int tab_bar_height(const struct terminal *term);
