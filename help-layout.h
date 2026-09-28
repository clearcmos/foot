#pragma once

#include <stdbool.h>

/* Lays the help card's entries out in columns. `blank[i]` marks the blank
 * lines between groups, the only places a column may break (a blank that
 * ends a column is dropped). Uses as few columns as keep each column
 * within `fit_rows` rows, then evens their heights out. A group taller
 * than `fit_rows` gets a column of its own and overflows it.
 *
 * Stores each non-blank entry's column and row in `col` and `row`, and
 * the tallest column's row count in `*rows`. Returns the column count. */
int help_layout_flow(const bool *blank, int count, int fit_rows,
                     int *col, int *row, int *rows);
