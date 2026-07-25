#pragma once

#include <stdbool.h>

struct terminal;

typedef bool (*tab_shutdown_fn)(struct terminal *term);

/* Returns the pre-removal tab index that should retain focus. */
int tab_close_focus_target(
    int tab_count, int closing_index, int active_index);

/* Starts terminal teardown while its shared window is still attached, then
 * detaches it before the deferred shutdown callback can destroy the window. */
bool tab_shutdown_and_detach(
    struct terminal *term, tab_shutdown_fn shutdown);
