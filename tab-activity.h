#pragma once

#include <stdbool.h>

/*
 * Returns true when process is an exact match for one of the comma-separated
 * names in configured_processes. Whitespace around each configured name is
 * ignored.
 */
bool tab_activity_process_matches(const char *configured_processes,
                                  const char *process);
