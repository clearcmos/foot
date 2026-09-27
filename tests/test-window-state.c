#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../window-state.h"

static struct window_state
read_str(const char *contents)
{
    FILE *f = fmemopen((void *)contents, strlen(contents), "r");
    assert(f != NULL);
    struct window_state s;
    window_state_read(f, &s);
    fclose(f);
    return s;
}

static struct window_state
round_trip(const struct window_state *in)
{
    char buf[512];
    FILE *f = fmemopen(buf, sizeof(buf), "w");
    assert(f != NULL);
    window_state_write(f, in);
    fclose(f);
    return read_str(buf);
}

static bool
near(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

static void
test_read_write(void)
{
    const struct window_state px = {
        .width = 1600, .height = 900, .maximized = true, .font_px = 18,
        .conf_size_type = 1, .conf_width = 80, .conf_height = 24,
        .has_font = true, .conf_font_pt = 12.f, .conf_font_px = -1,
    };
    struct window_state s = round_trip(&px);
    assert(s.width == 1600 && s.height == 900 && s.maximized);
    assert(s.font_px == 18);
    assert(window_state_size_matches(&s, 1, 80, 24));
    assert(window_state_font_matches(&s, 12.f, -1));

    const struct window_state pt = {
        .width = 800, .height = 600, .font_pt = 13.5f, .font_px = -1,
        .conf_size_type = 0, .conf_width = 700, .conf_height = 500,
        .has_font = true, .conf_font_pt = 11.f, .conf_font_px = -1,
    };
    s = round_trip(&pt);
    assert(!s.maximized);
    assert(near(s.font_pt, 13.5f) && s.font_px == 0);
    assert(window_state_size_matches(&s, 0, 700, 500));
    assert(window_state_font_matches(&s, 11.f, -1));

    /* No primary font: no font lines, so no font configuration matches */
    const struct window_state nofont = {
        .width = 800, .height = 600, .conf_size_type = 0,
        .conf_width = 700, .conf_height = 500,
    };
    s = round_trip(&nofont);
    assert(!s.has_font);
    assert(!window_state_font_matches(&s, -1.f, -1));

    /* Garbage and unknown keys are ignored */
    s = read_str("junk\nwidth=640\nheight=x\nfuture_key=1\nheight=480\n");
    assert(s.width == 640 && s.height == 480);
}

/* Before 5af8bf7c the saved size and zoom overrode an edited foot.ini; a
 * file from then, without the conf_* lines, must never match */
static void
test_legacy_file_never_matches(void)
{
    const struct window_state s =
        read_str("width=1600\nheight=900\nmaximized=1\nfont_pt=14.00\n");
    assert(s.width == 1600 && s.maximized);
    assert(!window_state_size_matches(&s, 0, 0, 0));
    assert(!window_state_size_matches(&s, 1, 80, 24));
    assert(!window_state_font_matches(&s, 14.f, -1));
    assert(!window_state_font_matches(&s, -1.f, -1));

    struct window_state_zoom zoom;
    assert(!window_state_zoom(&s, 12.f, -1, 96.f, &zoom));
}

static void
test_config_changes(void)
{
    const struct window_state s = read_str(
        "conf_size=1,80,24\nfont_pt=14.00\nconf_font=12.00,-1\n");

    /* Editing the window size or font in foot.ini wins over the file */
    assert(window_state_size_matches(&s, 1, 80, 24));
    assert(!window_state_size_matches(&s, 1, 100, 24));
    assert(!window_state_size_matches(&s, 0, 80, 24));
    assert(window_state_font_matches(&s, 12.004f, -1));
    assert(!window_state_font_matches(&s, 12.5f, -1));
    assert(!window_state_font_matches(&s, 12.f, 16));

    struct window_state_zoom zoom;
    assert(!window_state_zoom(&s, 13.f, -1, 96.f, &zoom));
}

static void
test_zoom_px(void)
{
    /* 12pt at 96 dpi is 16px; saved 20px means zoomed in by 4px */
    const struct window_state s = read_str(
        "font_px=20\nconf_font=12.00,-1\n");
    struct window_state_zoom zoom;
    assert(window_state_zoom(&s, 12.f, -1, 96.f, &zoom));
    assert(zoom.in_px && zoom.delta_px == 4);

    /* A secondary pt font keeps its relation: 10pt is 13px, now 17px */
    float pt = 10.f;
    int px = -1;
    window_state_zoom_font(&zoom, 96.f, &pt, &px);
    assert(px == 17);

    /* A px font moves by the same amount */
    pt = -1.f;
    px = 30;
    window_state_zoom_font(&zoom, 96.f, &pt, &px);
    assert(px == 34);

    /* Zooming out never goes below 1px */
    const struct window_state_zoom out = {.in_px = true, .delta_px = -50};
    pt = 10.f;
    px = -1;
    window_state_zoom_font(&out, 96.f, &pt, &px);
    assert(px == 1);

    /* Saved size equal to the configured one: nothing to apply */
    const struct window_state same = read_str(
        "font_px=16\nconf_font=12.00,-1\n");
    assert(!window_state_zoom(&same, 12.f, -1, 96.f, &zoom));
}

static void
test_zoom_pt(void)
{
    const struct window_state s = read_str(
        "font_pt=14.00\nconf_font=12.00,-1\n");
    struct window_state_zoom zoom;
    assert(window_state_zoom(&s, 12.f, -1, 96.f, &zoom));
    assert(!zoom.in_px && near(zoom.delta_pt, 2.f));

    float pt = 10.f;
    int px = -1;
    window_state_zoom_font(&zoom, 96.f, &pt, &px);
    assert(near(pt, 12.f) && px == -1);

    /* A px font is converted to points first: 20px at 96 dpi is 15pt */
    pt = -1.f;
    px = 20;
    window_state_zoom_font(&zoom, 96.f, &pt, &px);
    assert(near(pt, 17.f) && px == -1);

    const struct window_state_zoom out = {.delta_pt = -50.f};
    pt = 10.f;
    px = -1;
    window_state_zoom_font(&out, 96.f, &pt, &px);
    assert(near(pt, 0.f));

    const struct window_state same = read_str(
        "font_pt=12.00\nconf_font=12.00,-1\n");
    assert(!window_state_zoom(&same, 12.f, -1, 96.f, &zoom));
}

int
main(void)
{
    test_read_write();
    test_legacy_file_never_matches();
    test_config_changes();
    test_zoom_px();
    test_zoom_pt();
    return 0;
}
