#include "tab-close.h"

#include <stddef.h>

#include "terminal.h"

int
tab_close_focus_target(int tab_count, int closing_index, int active_index)
{
    if (tab_count <= 1 ||
        closing_index < 0 || closing_index >= tab_count ||
        active_index < 0 || active_index >= tab_count)
    {
        return -1;
    }

    if (closing_index != active_index)
        return active_index;

    return closing_index + 1 < tab_count
        ? closing_index + 1
        : closing_index - 1;
}

bool
tab_shutdown_and_detach(struct terminal *term, tab_shutdown_fn shutdown)
{
    bool ret = shutdown(term);
    term->window = NULL;
    return ret;
}
