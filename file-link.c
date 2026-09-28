#include "file-link.h"

#include <ctype.h>
#include <string.h>

#include "uri.h"
#include "util.h"
#include "xmalloc.h"

static bool
is_trailing_punct(char c)
{
    return c == '.' || c == ',' || c == ':' || c == ';' ||
        c == '!' || c == '?';
}

static void
trim_trailing_punct(char *s, size_t *len)
{
    /* A lone "." or "./" style prefix is part of the path, not
     * punctuation, so never trim the token down to nothing */
    while (*len > 1 && is_trailing_punct(s[*len - 1]))
        s[--*len] = '\0';
}

/* Removes one trailing ":digits" group; true if there was one */
static bool
strip_position(char *s, size_t *len)
{
    size_t i = *len;
    while (i > 0 && isdigit((unsigned char)s[i - 1]))
        i--;

    if (i == *len || i < 2 || s[i - 1] != ':')
        return false;

    *len = i - 1;
    s[*len] = '\0';
    return true;
}

void
file_link_clean(char *token)
{
    size_t len = strlen(token);

    trim_trailing_punct(token, &len);

    /* :line, then :column before it */
    if (strip_position(token, &len))
        strip_position(token, &len);
}

char *
file_link_resolve(const char *token, const char *cwd, const char *home)
{
    if (token == NULL || token[0] == '\0')
        return NULL;

    if (token[0] == '/')
        return xstrdup(token);

    if (token[0] == '~' && (token[1] == '\0' || token[1] == '/')) {
        if (home == NULL || home[0] == '\0')
            return NULL;
        return xasprintf("%s%s", home, token + 1);
    }

    if (cwd == NULL || cwd[0] != '/')
        return NULL;

    const size_t cwd_len = strlen(cwd);
    const bool slash = cwd_len > 0 && cwd[cwd_len - 1] == '/';
    return xasprintf("%s%s%s", cwd, slash ? "" : "/", token);
}

char *
file_link_path_from_uri(const char *uri)
{
    /* uri_parse() always stores the scheme, so it cannot be NULL */
    char *scheme = NULL;
    char *host = NULL;
    char *path = NULL;
    char *result = NULL;

    if (uri_parse(uri, strlen(uri), &scheme, NULL, NULL, &host, NULL,
                  &path, NULL, NULL) &&
        streq(scheme, "file") && path != NULL && path[0] == '/' &&
        (host == NULL || hostname_is_localhost(host)))
    {
        result = path;
        path = NULL;
    }

    free(scheme);
    free(host);
    free(path);
    return result;
}

char *
file_link_path_to_uri(const char *path)
{
    static const char hex[] = "0123456789ABCDEF";
    static const char prefix[] = "file://";

    const size_t path_len = strlen(path);
    char *uri = xmalloc(sizeof(prefix) + path_len * 3);
    memcpy(uri, prefix, sizeof(prefix) - 1);
    size_t len = sizeof(prefix) - 1;

    for (const char *p = path; *p != '\0'; p++) {
        const unsigned char c = (unsigned char)*p;
        /* ASCII only: isalnum() depends on the locale */
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '.' ||
            c == '_' || c == '~')
        {
            uri[len++] = (char)c;
        } else {
            uri[len++] = '%';
            uri[len++] = hex[c >> 4];
            uri[len++] = hex[c & 0xf];
        }
    }

    uri[len] = '\0';
    return uri;
}
