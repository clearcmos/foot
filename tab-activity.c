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
        if (!run->visible && run_finished_hidden(run)) {
            run->done = true;
            run->done_at = run->last_output + quiet_ms;
        }
        run->run_start = now;
    }

    run->last_output = now;
}

int64_t
tab_activity_done_age(const struct tab_activity_run *run, int64_t now,
                      uint32_t quiet_ms)
{
    if (run->visible)
        return -1;
    if (!in_run(run, now, quiet_ms) && run_finished_hidden(run))
        return now - (run->last_output + quiet_ms);
    return run->done ? now - run->done_at : -1;
}

enum tab_activity_state
tab_activity_state(const struct tab_activity_run *run, int64_t now,
                   uint32_t quiet_ms)
{
    if (run->visible)
        return TAB_ACTIVITY_NONE;

    if (in_run(run, now, quiet_ms) &&
        now - run->run_start >= TAB_ACTIVITY_MIN_RUN_MS)
    {
        return TAB_ACTIVITY_WORKING;
    }

    const int64_t age = tab_activity_done_age(run, now, quiet_ms);
    if (age < 0)
        return TAB_ACTIVITY_NONE;
    return age < TAB_ACTIVITY_FLASH_MS
        ? TAB_ACTIVITY_FINISHED
        : TAB_ACTIVITY_DONE;
}

double
tab_activity_flash_level(int64_t age_ms)
{
    if (age_ms <= 0)
        return 0.;
    if (age_ms >= TAB_ACTIVITY_FLASH_MS)
        return 1.;

    /* Triangle wave rising from 0, eased at its turning points */
    const double p = (double)(age_ms % TAB_ACTIVITY_FLASH_PERIOD_MS) /
        TAB_ACTIVITY_FLASH_PERIOD_MS;
    const double tri = p < .5 ? 2. * p : 2. - 2. * p;
    return tri * tri * (3. - 2. * tri);
}

bool
tab_activity_changing(const struct tab_activity_run *run, int64_t now,
                      uint32_t quiet_ms)
{
    return !run->visible &&
        (in_run(run, now, quiet_ms) ||
         tab_activity_state(run, now, quiet_ms) == TAB_ACTIVITY_FINISHED);
}
