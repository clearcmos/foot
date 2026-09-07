#include <stdbool.h>
#include <stdio.h>

#include "../tab-close.h"

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

int
main(void)
{
    return test_focus_target() ? 0 : 1;
}
