#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../file-link.h"

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n",                 \
                    __FILE__, __LINE__, #condition);                       \
            return false;                                                 \
        }                                                                 \
    } while (0)

static bool
cleans_to(const char *token, const char *want)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", token);
    file_link_clean(buf);
    if (strcmp(buf, want) != 0) {
        fprintf(stderr, "clean(\"%s\") = \"%s\", want \"%s\"\n",
                token, buf, want);
        return false;
    }
    return true;
}

static bool
test_clean(void)
{
    CHECK(cleans_to("render.c", "render.c"));
    CHECK(cleans_to("render.c:3058", "render.c"));
    CHECK(cleans_to("render.c:3058:12", "render.c"));
    CHECK(cleans_to("render.c:3058:12:", "render.c"));
    CHECK(cleans_to("src/tab.c:42,", "src/tab.c"));
    CHECK(cleans_to("CLAUDE.md.", "CLAUDE.md"));
    CHECK(cleans_to("~/arch/foot.ini;", "~/arch/foot.ini"));

    /* Only two position groups, and only digits count as one */
    CHECK(cleans_to("a:1:2:3", "a:1"));
    CHECK(cleans_to("host:path", "host:path"));
    CHECK(cleans_to("v1.2:", "v1.2"));

    /* Nothing is trimmed down to an empty token */
    CHECK(cleans_to(".", "."));
    CHECK(cleans_to(":12", ":12"));
    CHECK(cleans_to("", ""));
    return true;
}

static bool
resolves_to(const char *token, const char *cwd, const char *home,
            const char *want)
{
    char *got = file_link_resolve(token, cwd, home);
    const bool ok = want == NULL
        ? got == NULL
        : got != NULL && strcmp(got, want) == 0;
    if (!ok) {
        fprintf(stderr, "resolve(\"%s\") = \"%s\", want \"%s\"\n",
                token, got != NULL ? got : "(null)",
                want != NULL ? want : "(null)");
    }
    free(got);
    return ok;
}

static bool
test_resolve(void)
{
    CHECK(resolves_to("/etc/fstab", "/home/u", "/home/u", "/etc/fstab"));
    CHECK(resolves_to("tab.c", "/src/foot", "/home/u", "/src/foot/tab.c"));
    CHECK(resolves_to("doc/a.md", "/", "/home/u", "/doc/a.md"));
    CHECK(resolves_to("./x", "/src/", "/home/u", "/src/./x"));
    CHECK(resolves_to("~", "/src", "/home/u", "/home/u"));
    CHECK(resolves_to("~/arch/a", "/src", "/home/u", "/home/u/arch/a"));

    /* "~user" is not expanded; it is a relative name */
    CHECK(resolves_to("~bob/a", "/src", "/home/u", "/src/~bob/a"));

    CHECK(resolves_to("", "/src", "/home/u", NULL));
    CHECK(resolves_to("~/a", "/src", NULL, NULL));
    CHECK(resolves_to("a", NULL, "/home/u", NULL));
    CHECK(resolves_to("a", "relative", "/home/u", NULL));
    return true;
}

static bool
path_from_uri_is(const char *uri, const char *want)
{
    char *got = file_link_path_from_uri(uri);
    const bool ok = want == NULL
        ? got == NULL
        : got != NULL && strcmp(got, want) == 0;
    if (!ok) {
        fprintf(stderr, "path_from_uri(\"%s\") = \"%s\", want \"%s\"\n",
                uri, got != NULL ? got : "(null)",
                want != NULL ? want : "(null)");
    }
    free(got);
    return ok;
}

static bool
test_path_from_uri(void)
{
    CHECK(path_from_uri_is("file:///tmp/a%20b.txt", "/tmp/a b.txt"));
    CHECK(path_from_uri_is("file://localhost/etc/hosts", "/etc/hosts"));
    /* '#' and '?' are legal in file names, so they stay in the path */
    CHECK(path_from_uri_is("file:///tmp/a#b?c", "/tmp/a#b?c"));

    CHECK(path_from_uri_is("file://no-such-host.invalid/etc/hosts", NULL));
    CHECK(path_from_uri_is("https://example.com/a", NULL));
    CHECK(path_from_uri_is("mailto:a@example.com", NULL));
    CHECK(path_from_uri_is("not a uri", NULL));
    return true;
}

static bool
test_path_to_uri(void)
{
    char *uri = file_link_path_to_uri("/home/u/My Files/a,b~_-.c");
    CHECK(strcmp(uri, "file:///home/u/My%20Files/a%2Cb~_-.c") == 0);
    free(uri);

    uri = file_link_path_to_uri("/tmp/caf\xc3\xa9");
    CHECK(strcmp(uri, "file:///tmp/caf%C3%A9") == 0);
    free(uri);
    return true;
}

int
main(void)
{
    bool ok = true;
    ok = test_clean() && ok;
    ok = test_resolve() && ok;
    ok = test_path_from_uri() && ok;
    ok = test_path_to_uri() && ok;
    return ok ? 0 : 1;
}
