#include "tab-pin.h"

int
tab_pin_toggle_target(int pinned_count, bool was_pinned)
{
    return was_pinned ? pinned_count - 1 : pinned_count;
}

void
tab_pin_widths(int bar_width, int count, int pinned_count, int pinned_width,
               int *widths)
{
    if (count <= 0)
        return;

    const int equal_share = bar_width / count;
    if (pinned_width > equal_share)
        pinned_width = equal_share;

    for (int i = 0; i < pinned_count && i < count; i++)
        widths[i] = pinned_width;

    const int unpinned = count - pinned_count;
    if (unpinned <= 0)
        return;

    const int rest = bar_width - pinned_count * pinned_width;
    const int base = rest / unpinned;
    const int remainder = rest % unpinned;
    for (int i = 0; i < unpinned; i++)
        widths[pinned_count + i] = base + (i < remainder ? 1 : 0);
}
