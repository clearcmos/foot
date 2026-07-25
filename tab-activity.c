#include "tab-activity.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

bool
tab_activity_process_matches(const char *configured_processes,
                             const char *process)
{
    if (configured_processes == NULL || process == NULL || process[0] == '\0')
        return false;

    const size_t process_len = strlen(process);
    const char *p = configured_processes;

    while (*p != '\0') {
        while (*p == ',' || isspace((unsigned char)*p))
            p++;

        const char *start = p;
        while (*p != '\0' && *p != ',')
            p++;

        const char *end = p;
        while (end > start && isspace((unsigned char)end[-1]))
            end--;

        const size_t len = (size_t)(end - start);
        if (len == process_len && strncmp(start, process, len) == 0)
            return true;
    }

    return false;
}
