#include <stdbool.h>
#include <stdio.h>

#include "../tab-pin.h"

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n",                 \
                    __FILE__, __LINE__, #condition);                       \
            return false;                                                 \
        }                                                                 \
    } while (0)

static bool
test_toggle_target(void)
{
    /* Pinning appends to the pinned group. */
    CHECK(tab_pin_toggle_target(0, false) == 0);
    CHECK(tab_pin_toggle_target(2, false) == 2);

    /* Unpinning lands on the first unpinned slot. */
    CHECK(tab_pin_toggle_target(1, true) == 0);
    CHECK(tab_pin_toggle_target(3, true) == 2);
    return true;
}

static bool
test_widths(void)
{
    int w[4];

    /* No pinned tabs: equal widths, remainder to the left. */
    tab_pin_widths(10, 3, 0, 2, w);
    CHECK(w[0] == 4 && w[1] == 3 && w[2] == 3);

    /* Pinned tabs are compact; unpinned tabs share the rest. */
    tab_pin_widths(100, 3, 1, 20, w);
    CHECK(w[0] == 20 && w[1] == 40 && w[2] == 40);

    /* A pinned tab is never wider than an equal share. */
    tab_pin_widths(30, 3, 2, 50, w);
    CHECK(w[0] == 10 && w[1] == 10 && w[2] == 10);

    /* All pinned: compact widths, the rest of the bar stays empty. */
    tab_pin_widths(100, 2, 2, 20, w);
    CHECK(w[0] == 20 && w[1] == 20);
    return true;
}

int
main(void)
{
    return test_toggle_target() && test_widths() ? 0 : 1;
}
