#include <stdbool.h>
#include <stdio.h>

#include "../tab-close.h"
#include "../terminal.h"

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n",                 \
                    __FILE__, __LINE__, #condition);                       \
            return false;                                                 \
        }                                                                 \
    } while (0)

static bool
test_focus_target(void)
{
    static const struct {
        int tab_count;
        int closing_index;
        int active_index;
        int expected;
    } cases[] = {
        /* Closing an inactive tab must preserve the active tab. */
        {3, 0, 1, 1},
        {3, 2, 1, 1},

        /* Regression: an active tab prefers its right neighbor. */
        {2, 0, 0, 1},
        {3, 1, 1, 2},

        /* Regression: the rightmost active tab falls back to the left. */
        {2, 1, 1, 0},
        {3, 2, 2, 1},

        /* Invalid and last-tab closes have no focus target. */
        {1, 0, 0, -1},
        {3, -1, 0, -1},
        {3, 3, 0, -1},
        {3, 0, -1, -1},
        {3, 0, 3, -1},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(tab_close_focus_target(
                  cases[i].tab_count,
                  cases[i].closing_index,
                  cases[i].active_index) == cases[i].expected);
    }

    return true;
}

static struct wl_window *expected_window;
static bool shutdown_result;
static int shutdown_calls;

static bool
shutdown_stub(struct terminal *term)
{
    CHECK(term->window == expected_window);
    shutdown_calls++;
    return shutdown_result;
}

static bool
test_shutdown_order(void)
{
    struct terminal term = {0};
    term.window = (struct wl_window *)&term;
    expected_window = term.window;

    /* Regression: shutdown must see the configured window so it unregisters
     * the PTY before the tab detaches from the shared Wayland window. */
    shutdown_result = true;
    shutdown_calls = 0;
    CHECK(tab_shutdown_and_detach(&term, &shutdown_stub));
    CHECK(shutdown_calls == 1);
    CHECK(term.window == NULL);

    /* A failed asynchronous setup must still detach the closing tab so its
     * deferred cleanup cannot destroy the window owned by remaining tabs. */
    term.window = expected_window;
    shutdown_result = false;
    shutdown_calls = 0;
    CHECK(!tab_shutdown_and_detach(&term, &shutdown_stub));
    CHECK(shutdown_calls == 1);
    CHECK(term.window == NULL);

    return true;
}

int
main(void)
{
    if (!test_focus_target() || !test_shutdown_order())
        return 1;

    return 0;
}
