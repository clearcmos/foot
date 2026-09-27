#pragma once

#include <stdbool.h>
#include <stdio.h>

/*
 * Saved window state: the last floating size, maximized state and zoom,
 * plus the configuration in effect when it was saved. A value is only
 * restored while that configuration is unchanged, so edits to foot.ini
 * and -w/-W take effect immediately.
 */
struct window_state {
    int width, height;             /* physical pixels */
    bool maximized;
    float font_pt;                 /* zoomed primary font size, one of */
    int font_px;
    int conf_size_type;            /* configuration at save time */
    unsigned conf_width, conf_height;
    bool has_font;                 /* conf_font_* are valid */
    float conf_font_pt;
    int conf_font_px;
};

/* Reads a state file. Unknown or malformed lines are ignored, and a file
 * without the configuration lines never matches a configuration. */
void window_state_read(FILE *f, struct window_state *s);

void window_state_write(FILE *f, const struct window_state *s);

bool window_state_size_matches(const struct window_state *s, int size_type,
                               unsigned width, unsigned height);

bool window_state_font_matches(const struct window_state *s,
                               float conf_pt, int conf_px);

/*
 * The uniform adjustment that zooms the configured primary font to the
 * saved size, as the zoom key bindings make it: in pixels when the saved
 * size is in pixels, else in points.
 */
struct window_state_zoom {
    bool in_px;
    int delta_px;
    float delta_pt;
};

/* False when the font configuration changed or there is no zoom */
bool window_state_zoom(const struct window_state *s, float conf_pt,
                       int conf_px, float dpi,
                       struct window_state_zoom *zoom);

/* Applies zoom to one configured font size, in place */
void window_state_zoom_font(const struct window_state_zoom *zoom, float dpi,
                            float *pt_size, int *px_size);
