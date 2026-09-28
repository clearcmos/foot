#include "help-layout.h"

/* Greedy fill: a group moves to a new column when it would push the
 * current one past cap rows */
static int
flow(const bool *blank, int count, int cap, int *col, int *row, int *rows)
{
    int cols = 1;
    int r = 0;
    *rows = 0;

    for (int i = 0; i < count; i++) {
        int end = i;
        while (end < count && !blank[end])
            end++;

        if (r > 0 && r + 1 + (end - i) > cap) {
            cols++;
            r = 0;
        } else if (r > 0)
            r++;

        for (; i < end; i++) {
            col[i] = cols - 1;
            row[i] = r++;
        }
        if (r > *rows)
            *rows = r;
    }

    return cols;
}

int
help_layout_flow(const bool *blank, int count, int fit_rows,
                 int *col, int *row, int *rows)
{
    const int cols = flow(blank, count, fit_rows, col, row, rows);
    if (cols == 1)
        return cols;

    /* The shortest cap that needs no more columns balances them */
    for (int cap = 1; cap < fit_rows; cap++) {
        if (flow(blank, count, cap, col, row, rows) <= cols)
            return cols;
    }

    return flow(blank, count, fit_rows, col, row, rows);
}
