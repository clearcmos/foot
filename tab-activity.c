#include "tab-activity.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

struct entry {
    const char *name;
    size_t name_len;
    const char *color;  /* NULL when the entry has no color suffix */
    size_t color_len;
};

static const char *
trim_end(const char *start, const char *end)
{
    while (end > start && isspace((unsigned char)end[-1]))
        end--;
    return end;
}

/* Parses the entry at *p and advances past it. False at the end of the list. */
static bool
next_entry(const char **p, struct entry *e)
{
    const char *s = *p;
    while (*s == ',' || isspace((unsigned char)*s))
        s++;

    if (*s == '\0') {
        *p = s;
        return false;
    }

    const char *start = s;
    while (*s != '\0' && *s != ',')
        s++;
    *p = s;

    const char *end = trim_end(start, s);
    const char *colon = memchr(start, ':', (size_t)(end - start));

    e->name = start;
    e->name_len = (size_t)(trim_end(start, colon != NULL ? colon : end) - start);

    if (colon == NULL) {
        e->color = NULL;
        e->color_len = 0;
    } else {
        const char *c = colon + 1;
        while (c < end && isspace((unsigned char)*c))
            c++;
        e->color = c;
        e->color_len = (size_t)(end - c);
    }

    return true;
}

static bool
parse_rgb(const char *s, size_t len, uint32_t *color)
{
    if (len != 6)
        return false;

    uint32_t value = 0;
    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (!isxdigit(c))
            return false;
        value = (value << 4) |
            (uint32_t)(isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
    }

    *color = value;
    return true;
}

bool
tab_activity_process_color(const char *configured_processes,
                           const char *process, uint32_t default_color,
                           uint32_t *color)
{
    if (configured_processes == NULL || process == NULL || process[0] == '\0')
        return false;

    const size_t process_len = strlen(process);
    const char *p = configured_processes;
    struct entry e;

    while (next_entry(&p, &e)) {
        if (e.name_len != process_len ||
            strncmp(e.name, process, process_len) != 0)
        {
            continue;
        }

        if (color != NULL &&
            (e.color == NULL || !parse_rgb(e.color, e.color_len, color)))
        {
            *color = default_color;
        }
        return true;
    }

    return false;
}

bool
tab_activity_process_matches(const char *configured_processes,
                             const char *process)
{
    return tab_activity_process_color(
        configured_processes, process, 0, NULL);
}

bool
tab_activity_processes_valid(const char *configured_processes)
{
    if (configured_processes == NULL)
        return true;

    const char *p = configured_processes;
    struct entry e;
    uint32_t color;

    while (next_entry(&p, &e)) {
        if (e.name_len == 0)
            return false;
        if (e.color != NULL && !parse_rgb(e.color, e.color_len, &color))
            return false;
    }

    return true;
}

/* The latest run lasted long enough and produced output after the tab
 * was hidden */
static bool
run_finished_hidden(const struct tab_activity_run *run)
{
    return run->last_output - run->run_start >= TAB_ACTIVITY_MIN_RUN_MS &&
        run->last_output > run->hidden_since;
}

static bool
in_run(const struct tab_activity_run *run, int64_t now, uint32_t quiet_ms)
{
    return run->last_output > 0 && now - run->last_output < quiet_ms;
}

void
tab_activity_set_visible(struct tab_activity_run *run, bool visible,
                         int64_t now)
{
    if (visible == run->visible)
        return;

    run->visible = visible;
    if (visible)
        run->done = false;
    else
        run->hidden_since = now;
}

void
tab_activity_record_output(struct tab_activity_run *run, int64_t now,
                           uint32_t quiet_ms)
{
    if (!in_run(run, now, quiet_ms)) {
        /* A new run starts; keep the mark the previous one earned */
        if (!run->visible && run_finished_hidden(run))
            run->done = true;
        run->run_start = now;
    }

    run->last_output = now;
}

enum tab_activity_state
tab_activity_state(const struct tab_activity_run *run, int64_t now,
                   uint32_t quiet_ms)
{
    if (run->visible)
        return TAB_ACTIVITY_NONE;

    if (in_run(run, now, quiet_ms)) {
        if (now - run->run_start >= TAB_ACTIVITY_MIN_RUN_MS)
            return TAB_ACTIVITY_WORKING;
    } else if (run_finished_hidden(run)) {
        return TAB_ACTIVITY_DONE;
    }

    return run->done ? TAB_ACTIVITY_DONE : TAB_ACTIVITY_NONE;
}

bool
tab_activity_run_pending(const struct tab_activity_run *run, int64_t now,
                         uint32_t quiet_ms)
{
    return !run->visible && in_run(run, now, quiet_ms);
}
