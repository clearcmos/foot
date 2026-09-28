#pragma once

#include <stdbool.h>

/*
 * Pure helpers for Ctrl+clicking file paths in terminal output. The
 * clicked text is cleaned and resolved here; checking that the file
 * exists and launching the file manager happen in url-mode.c.
 */

/* Strips what commonly trails a path in compiler, grep and agent output:
 * sentence punctuation and a :line or :line:column position, as in
 * "render.c:3058:12," -> "render.c". Edits token in place. */
void file_link_clean(char *token);

/* Absolute path for a cleaned token: "~" and "~/..." expand to home, a
 * relative path is taken relative to cwd. Returns NULL for an empty token,
 * or when home or cwd is needed but missing. Caller frees. */
char *file_link_resolve(const char *token, const char *cwd, const char *home);

/* Local path named by a file: URI, percent-decoded, or NULL when uri is
 * not a file: URI, has no absolute path, or names another host. Caller
 * frees. */
char *file_link_path_from_uri(const char *uri);

/* "file://" URI for an absolute path, percent-encoding every byte other
 * than unreserved characters and '/'. Caller frees. */
char *file_link_path_to_uri(const char *path);
