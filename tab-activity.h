#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Process lists are comma-separated entries of the form name[:RRGGBB].
 * Whitespace around names and colors is ignored.
 */

/*
 * Returns true when process is an exact match for one of the names in
 * configured_processes. Any color suffix is ignored.
 */
bool tab_activity_process_matches(const char *configured_processes,
                                  const char *process);

/*
 * Like tab_activity_process_matches(), but on a match also stores the
 * matching entry's color in *color, or default_color if the entry has none.
 */
bool tab_activity_process_color(const char *configured_processes,
                                const char *process, uint32_t default_color,
                                uint32_t *color);

/*
 * Returns true when every entry has a non-empty name and any color suffix
 * is six hex digits.
 */
bool tab_activity_processes_valid(const char *configured_processes);

/*
 * Output from a matching process in a hidden tab first marks the tab as
 * working. Once it stops, the tab flashes (finished) and then stays marked
 * (done) until it is shown. Only a run of output lasting
 * TAB_ACTIVITY_MIN_RUN_MS counts, so one-off redraws, like a TUI repainting
 * when it loses focus, do not mark a tab. A run ends after quiet_ms without
 * output.
 */
#define TAB_ACTIVITY_MIN_RUN_MS 1000

/* The finish flash: three pulses, ending on the third peak so it runs
 * into the done mark at full brightness */
#define TAB_ACTIVITY_FLASH_PERIOD_MS 800
#define TAB_ACTIVITY_FLASH_MS 2000

enum tab_activity_state {
    TAB_ACTIVITY_NONE,
    TAB_ACTIVITY_WORKING,
    TAB_ACTIVITY_FINISHED,
    TAB_ACTIVITY_DONE,
};

/* Times are milliseconds on a monotonic clock. Zero-initialize. */
struct tab_activity_run {
    int64_t run_start;      /* first output of the latest run */
    int64_t last_output;    /* latest output */
    int64_t hidden_since;   /* when the tab last stopped being visible */
    int64_t done_at;        /* when the run behind the done mark ended */
    bool visible;
    bool done;              /* an earlier run finished while hidden */
};

/* Records that the tab is (or is not) visible. Showing the tab clears
 * its done mark. */
void tab_activity_set_visible(struct tab_activity_run *run, bool visible,
                              int64_t now);

/* Records output from a matching process. */
void tab_activity_record_output(struct tab_activity_run *run, int64_t now,
                                uint32_t quiet_ms);

enum tab_activity_state tab_activity_state(const struct tab_activity_run *run,
                                           int64_t now, uint32_t quiet_ms);

/* How long ago the run behind a hidden tab's done mark ended, or -1 when
 * the tab has no mark. */
int64_t tab_activity_done_age(const struct tab_activity_run *run,
                              int64_t now, uint32_t quiet_ms);

/* Brightness of the finish flash in [0, 1], age_ms after the run ended.
 * Starts at 0 and stays at 1 from TAB_ACTIVITY_FLASH_MS on. */
double tab_activity_flash_level(int64_t age_ms);

/* True while the indicator of a hidden tab can still change without
 * further output: a run of output is ongoing or the finish flash plays. */
bool tab_activity_changing(const struct tab_activity_run *run,
                           int64_t now, uint32_t quiet_ms);
