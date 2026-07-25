#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "../tab-activity.h"

int
main(void)
{
    assert(tab_activity_process_matches("claude", "claude"));
    assert(tab_activity_process_matches("claude,codex", "codex"));
    assert(tab_activity_process_matches(" claude, codex ", "claude"));
    assert(tab_activity_process_matches(" claude, codex ", "codex"));
    assert(tab_activity_process_matches(",,,claude,,,", "claude"));

    assert(!tab_activity_process_matches(NULL, "claude"));
    assert(!tab_activity_process_matches("", "claude"));
    assert(!tab_activity_process_matches("   ", "claude"));
    assert(!tab_activity_process_matches("claude", NULL));
    assert(!tab_activity_process_matches("claude", ""));
    assert(!tab_activity_process_matches("claude", "claud"));
    assert(!tab_activity_process_matches("claude-code", "claude"));
    assert(!tab_activity_process_matches("other,codex", "claude"));

    return 0;
}
