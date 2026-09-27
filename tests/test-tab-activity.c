#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../tab-activity.h"

#define QUIET 700

/* Feeds output every 100 ms over [from, to) */
static void
output_run(struct tab_activity_run *run, int64_t from, int64_t to)
{
    for (int64_t t = from; t < to; t += 100)
        tab_activity_record_output(run, t, QUIET);
}

static void
test_activity_state(void)
{
    /* Sustained output in a hidden tab pulses, then marks the tab done */
    struct tab_activity_run run = {0};
    tab_activity_set_visible(&run, false, 1000);
    output_run(&run, 10000, 10500);
    assert(tab_activity_state(&run, 10500, QUIET) == TAB_ACTIVITY_NONE);
    assert(tab_activity_run_pending(&run, 10500, QUIET));
    output_run(&run, 10500, 12000);
    assert(tab_activity_state(&run, 11950, QUIET) == TAB_ACTIVITY_WORKING);
    assert(tab_activity_state(&run, 13000, QUIET) == TAB_ACTIVITY_DONE);
    assert(!tab_activity_run_pending(&run, 13000, QUIET));

    /* A later one-off redraw keeps the mark */
    tab_activity_record_output(&run, 20000, QUIET);
    assert(tab_activity_state(&run, 20100, QUIET) == TAB_ACTIVITY_DONE);
    assert(tab_activity_state(&run, 21000, QUIET) == TAB_ACTIVITY_DONE);

    /* Showing the tab clears it, and hiding it again does not bring it back */
    tab_activity_set_visible(&run, true, 30000);
    assert(tab_activity_state(&run, 30000, QUIET) == TAB_ACTIVITY_NONE);
    tab_activity_set_visible(&run, false, 31000);
    assert(tab_activity_state(&run, 31000, QUIET) == TAB_ACTIVITY_NONE);

    /* A redraw on losing focus is too short to count */
    tab_activity_record_output(&run, 31050, QUIET);
    assert(tab_activity_state(&run, 31100, QUIET) == TAB_ACTIVITY_NONE);
    assert(tab_activity_state(&run, 32000, QUIET) == TAB_ACTIVITY_NONE);

    /* Visible tabs never show activity */
    struct tab_activity_run shown = {0};
    tab_activity_set_visible(&shown, true, 1000);
    output_run(&shown, 10000, 12000);
    assert(tab_activity_state(&shown, 11950, QUIET) == TAB_ACTIVITY_NONE);
    assert(!tab_activity_run_pending(&shown, 11950, QUIET));

    /* A run watched to its end is not marked after leaving the tab */
    tab_activity_set_visible(&shown, false, 13000);
    assert(tab_activity_state(&shown, 13000, QUIET) == TAB_ACTIVITY_NONE);

    /* A run that started while visible and ends after leaving is marked */
    struct tab_activity_run left = {0};
    tab_activity_set_visible(&left, true, 1000);
    output_run(&left, 10000, 11500);
    tab_activity_set_visible(&left, false, 11500);
    output_run(&left, 11500, 12000);
    assert(tab_activity_state(&left, 11950, QUIET) == TAB_ACTIVITY_WORKING);
    assert(tab_activity_state(&left, 13000, QUIET) == TAB_ACTIVITY_DONE);
}

int
main(void)
{
    test_activity_state();

    assert(tab_activity_process_matches("claude", "claude"));
    assert(tab_activity_process_matches("claude,codex", "codex"));
    assert(tab_activity_process_matches(" claude, codex ", "claude"));
    assert(tab_activity_process_matches(" claude, codex ", "codex"));
    assert(tab_activity_process_matches(",,,claude,,,", "claude"));
    assert(tab_activity_process_matches("claude:d97757,codex", "claude"));
    assert(tab_activity_process_matches("claude:d97757,codex", "codex"));

    assert(!tab_activity_process_matches(NULL, "claude"));
    assert(!tab_activity_process_matches("", "claude"));
    assert(!tab_activity_process_matches("   ", "claude"));
    assert(!tab_activity_process_matches("claude", NULL));
    assert(!tab_activity_process_matches("claude", ""));
    assert(!tab_activity_process_matches("claude", "claud"));
    assert(!tab_activity_process_matches("claude-code", "claude"));
    assert(!tab_activity_process_matches("other,codex", "claude"));
    assert(!tab_activity_process_matches("claude:d97757", "claude:d97757"));

    const char *list = "claude:d97757, codex : 10A37F ,agy:1a73e8,nano";
    uint32_t color = 0;
    assert(tab_activity_process_color(list, "claude", 0x00cc33, &color));
    assert(color == 0xd97757);
    assert(tab_activity_process_color(list, "codex", 0x00cc33, &color));
    assert(color == 0x10a37f);
    assert(tab_activity_process_color(list, "agy", 0x00cc33, &color));
    assert(color == 0x1a73e8);
    assert(tab_activity_process_color(list, "nano", 0x00cc33, &color));
    assert(color == 0x00cc33);
    color = 0x123456;
    assert(!tab_activity_process_color(list, "vim", 0x00cc33, &color));
    assert(color == 0x123456);

    assert(tab_activity_processes_valid(NULL));
    assert(tab_activity_processes_valid(""));
    assert(tab_activity_processes_valid("claude"));
    assert(tab_activity_processes_valid(list));
    assert(!tab_activity_processes_valid("claude:"));
    assert(!tab_activity_processes_valid("claude:d9775"));
    assert(!tab_activity_processes_valid("claude:d97757ff"));
    assert(!tab_activity_processes_valid("claude:g97757"));
    assert(!tab_activity_processes_valid(":d97757"));

    return 0;
}
