#include <stdbool.h>
#include <stdio.h>

#include "../help-layout.h"

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "%s:%d: check failed: %s\n",                 \
                    __FILE__, __LINE__, #condition);                       \
            return false;                                                 \
        }                                                                 \
    } while (0)

/* The help card's shape: groups of 9, 4, 4, 3 and 1 entries */
#define COUNT 25
static const bool blank[COUNT] = {
    [9] = true, [14] = true, [19] = true, [23] = true,
};

static bool
test_single_column(void)
{
    int col[COUNT], row[COUNT], rows;

    /* Blank lines keep their rows when nothing breaks */
    CHECK(help_layout_flow(blank, COUNT, 25, col, row, &rows) == 1);
    CHECK(rows == 25);
    CHECK(col[24] == 0 && row[24] == 24);
    CHECK(row[10] == 10);

    CHECK(help_layout_flow(blank, COUNT, 100, col, row, &rows) == 1);
    CHECK(rows == 25);
    return true;
}

static bool
test_balanced_columns(void)
{
    int col[COUNT], row[COUNT], rows;

    /* One row short: two columns of 14 and 10 rows, not 23 and 1 */
    CHECK(help_layout_flow(blank, COUNT, 24, col, row, &rows) == 2);
    CHECK(rows == 14);
    CHECK(col[13] == 0 && row[13] == 13);
    CHECK(col[15] == 1 && row[15] == 0);
    CHECK(col[24] == 1 && row[24] == 9);

    /* A column break drops the blank line */
    CHECK(help_layout_flow(blank, COUNT, 12, col, row, &rows) == 3);
    CHECK(rows == 9);
    CHECK(col[10] == 1 && row[10] == 0);
    for (int i = 0; i < COUNT; i++)
        CHECK(blank[i] || row[i] < 12);
    return true;
}

static bool
test_overflow(void)
{
    int col[COUNT], row[COUNT], rows;

    /* Groups taller than the window each take a column and overflow */
    CHECK(help_layout_flow(blank, COUNT, 5, col, row, &rows) == 4);
    CHECK(rows == 9);
    CHECK(help_layout_flow(blank, COUNT, 0, col, row, &rows) == 5);
    CHECK(rows == 9);
    CHECK(col[24] == 4 && row[24] == 0);
    return true;
}

int
main(void)
{
    bool ok = true;
    ok = test_single_column() && ok;
    ok = test_balanced_columns() && ok;
    ok = test_overflow() && ok;
    return ok ? 0 : 1;
}
