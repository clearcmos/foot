#pragma once

#include <stdbool.h>

/* Pinned tabs form a contiguous group at the front of the tab list.
 * Returns the index a toggled tab moves to, counted after it has been
 * removed from the list: the end of the pinned group when pinning, the
 * first unpinned slot when unpinning. `pinned_count` is the size of the
 * pinned group before the toggle and `was_pinned` the tab's state. */
int tab_pin_toggle_target(int pinned_count, bool was_pinned);

/* Split `bar_width` pixels between `count` tabs, the first
 * `pinned_count` of which are pinned. Pinned tabs get `pinned_width`,
 * capped at an equal share; unpinned tabs divide the rest evenly, with
 * remainder pixels going to the leftmost of them. */
void tab_pin_widths(int bar_width, int count, int pinned_count,
                    int pinned_width, int *widths);

