#include "window-state.h"

#include <math.h>

void
window_state_read(FILE *f, struct window_state *s)
{
    /* Sentinels: a file without the conf_* lines never matches */
    *s = (struct window_state){
        .conf_size_type = -1, .conf_font_pt = -1., .conf_font_px = -1};

    char line[256];
    int maximized = 0;
    unsigned conf_w = 0, conf_h = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        if (sscanf(line, "width=%d", &s->width) == 1) continue;
        if (sscanf(line, "height=%d", &s->height) == 1) continue;
        if (sscanf(line, "maximized=%d", &maximized) == 1) continue;
        if (sscanf(line, "font_pt=%f", &s->font_pt) == 1) continue;
        if (sscanf(line, "font_px=%d", &s->font_px) == 1) continue;
        if (sscanf(line, "conf_size=%d,%u,%u",
                   &s->conf_size_type, &conf_w, &conf_h) == 3)
        {
            s->conf_width = conf_w;
            s->conf_height = conf_h;
            continue;
        }

        float font_pt;
        int font_px;
        if (sscanf(line, "conf_font=%f,%d", &font_pt, &font_px) == 2) {
            s->has_font = true;
            s->conf_font_pt = font_pt;
            s->conf_font_px = font_px;
        }
    }

    s->maximized = maximized != 0;
}

void
window_state_write(FILE *f, const struct window_state *s)
{
    fprintf(f, "width=%d\n", s->width);
    fprintf(f, "height=%d\n", s->height);
    fprintf(f, "maximized=%d\n", s->maximized ? 1 : 0);
    fprintf(f, "conf_size=%d,%u,%u\n",
            s->conf_size_type, s->conf_width, s->conf_height);

    if (s->has_font) {
        if (s->font_px > 0)
            fprintf(f, "font_px=%d\n", s->font_px);
        else
            fprintf(f, "font_pt=%.2f\n", s->font_pt);
        fprintf(f, "conf_font=%.2f,%d\n", s->conf_font_pt, s->conf_font_px);
    }
}

bool
window_state_size_matches(const struct window_state *s, int size_type,
                          unsigned width, unsigned height)
{
    return s->conf_size_type == size_type &&
        s->conf_width == width &&
        s->conf_height == height;
}

bool
window_state_font_matches(const struct window_state *s,
                          float conf_pt, int conf_px)
{
    return s->has_font &&
        s->conf_font_px == conf_px &&
        fabsf(s->conf_font_pt - conf_pt) < 0.005f;
}

bool
window_state_zoom(const struct window_state *s, float conf_pt, int conf_px,
                  float dpi, struct window_state_zoom *zoom)
{
    if (!window_state_font_matches(s, conf_pt, conf_px))
        return false;

    if (s->font_px > 0) {
        const int primary_px = conf_px > 0
            ? conf_px
            : (int)roundf(conf_pt * dpi / 72.);
        *zoom = (struct window_state_zoom){
            .in_px = true, .delta_px = s->font_px - primary_px};
        return zoom->delta_px != 0;
    }

    if (s->font_pt > 0) {
        const float primary_pt = conf_px > 0 ? conf_px * 72. / dpi : conf_pt;
        *zoom = (struct window_state_zoom){
            .in_px = false, .delta_pt = s->font_pt - primary_pt};
        return fabsf(zoom->delta_pt) >= 0.005f;
    }

    return false;
}

void
window_state_zoom_font(const struct window_state_zoom *zoom, float dpi,
                       float *pt_size, int *px_size)
{
    if (zoom->in_px) {
        const int px = *px_size > 0
            ? *px_size
            : (int)roundf(*pt_size * dpi / 72.);
        const int zoomed = px + zoom->delta_px;
        *px_size = zoomed > 1 ? zoomed : 1;
    } else {
        const float pt = *px_size > 0 ? *px_size * 72. / dpi : *pt_size;
        *pt_size = fmaxf(pt + zoom->delta_pt, 0.f);
        *px_size = -1;
    }
}
