#pragma once

/* Returns the pre-removal tab index that should retain focus, or -1 when
 * the indices are invalid or only one tab remains. */
int tab_close_focus_target(
    int tab_count, int closing_index, int active_index);
