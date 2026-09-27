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
 * Output from a matching process in a hidden tab first pulses (working),
 * then leaves the tab marked (done) once it stops, until the tab is shown.
 * Only a run of output lasting TAB_ACTIVITY_MIN_RUN_MS counts, so one-off
 * redraws, like a TUI repainting when it loses focus, do not mark a tab.
 * A run ends after quiet_ms without output.
 */
#define TAB_ACTIVITY_MIN_RUN_MS 1000

enum tab_activity_state {
    TAB_ACTIVITY_NONE,
    TAB_ACTIVITY_WORKING,
    TAB_ACTIVITY_DONE,
};

/* Times are milliseconds on a monotonic clock. Zero-initialize. */
struct tab_activity_run {
    int64_t run_start;      /* first output of the latest run */
    int64_t last_output;    /* latest output */
    int64_t hidden_since;   /* when the tab last stopped being visible */
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

/* True while a run of output in a hidden tab is ongoing, so the state can
 * still change without further output. */
bool tab_activity_run_pending(const struct tab_activity_run *run,
                              int64_t now, uint32_t quiet_ms);
