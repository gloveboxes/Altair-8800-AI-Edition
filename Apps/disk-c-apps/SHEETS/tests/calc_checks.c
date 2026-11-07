/* Exercise the actual 16-bit-int / 32-bit-long CP/M calculator build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SHEETS.H"

int dirty = 0;
static int failures;
unsigned svc_random_u16(void) { return 7; }

static void put(int row, int column, const char *text)
{
    if (set_cell(row, column, text) != 0) {
        puts("FAIL allocation");
        exit(1);
    }
}

static void expect(int row, int column, const char *wanted)
{
    char actual[CELL_WIDTH + 1];
    render_cell(row, column, actual);
    if (strcmp(actual, wanted) != 0) {
        printf("FAIL %c%d expected [%s], got [%s]\n", 'A' + column, row + 1, wanted, actual);
        ++failures;
    }
}

static void reset(void)
{
    int row;
    int column;
    for (row = 0; row < MAX_ROWS; ++row)
        for (column = 0; column < MAX_COLUMNS; ++column)
            clear_cell(row, column);
    invalidate_formula_cache();
}

int main(void)
{
    int row;
    char formula[160];
    static const char *numbers[] = {
        "0", "1", "9", "10", "99", "100", "1000", "10000", "100000",
        "1000000", "10000000", "100000000", "1000000000", "2147483647",
        "-1", "-9", "-10", "-10000", "-999999999", "-2147483648"
    };

    expect(0, 0, "          ");
    put(0, 0, "Hi");
    expect(0, 0, "Hi        ");
    put(0, 0, "abcdefghijkl");
    expect(0, 0, "abcdefghij");
    put(0, 0, "12345678901");
    expect(0, 0, "##########");

    /* Decimal formatting boundaries, including LONG_MIN, and numeric tails. */
    for (row = 0; row < (int)(sizeof(numbers) / sizeof(numbers[0])); ++row) {
        char expected[CELL_WIDTH + 1];
        unsigned length = strlen(numbers[row]);
        memset(expected, ' ', CELL_WIDTH);
        expected[CELL_WIDTH] = 0;
        if (length > CELL_WIDTH)
            memset(expected, '#', CELL_WIDTH);
        else
            memcpy(expected + CELL_WIDTH - length, numbers[row], length);
        put(0, 0, numbers[row]);
        put(0, 1, "=A1");
        expect(0, 1, expected);
        expect(0, 1, expected);
    }
    put(0, 0, "  12trailing  ");
    expect(0, 1, "        12");
    put(0, 0, "-");
    expect(0, 1, "         0");
    put(0, 0, "+12");
    expect(0, 1, "         0");
    strcpy(formula, "=1");
    memset(formula + 2, ' ', 90);
    strcpy(formula + 92, " + 2 * ( 3 + 4 )");
    put(1, 1, formula); /* Long file-loaded formulas bypass normalization. */
    expect(1, 1, "        15");
    strcpy(formula, "=SUM(");
    memset(formula + 5, ' ', 90);
    strcpy(formula + 95, " A1 : A2 )");
    put(2, 1, formula);
    expect(2, 1, "         0");

    reset();
    put(0, 0, "10");
    put(0, 1, "= $a$1 * 2");
    put(0, 2, "=B1+B1");
    expect(0, 1, "        20");
    expect(0, 2, "        40");
    put(98, 25, "99"); /* Unrelated edit preserves valid display results. */
    expect(0, 2, "        40");
    put(0, 0, "11");
    expect(0, 2, "        44"); /* Transitive dependency through cached B1. */
    clear_cell(0, 0);
    expect(0, 2, "         0");
    put(0, 0, "7");
    expect(0, 2, "        28");
    put(0, 1, "=A1*3");
    expect(0, 2, "        42");

    /* Direct-map collisions must not corrupt values or dependency bounds. */
    put(2, 13, "=A1*5"); /* N3 and B1 share the same cache slot. */
    put(1, 2, "=B1+N3+B1+N3");
    expect(1, 2, "       112");
    put(0, 0, "2");
    expect(1, 2, "        32");
    put(4, 25, "=A1*7"); /* A third entry forces replacement in that bucket. */
    put(1, 3, "=B1+N3+Z5+B1+N3+Z5");
    expect(1, 3, "        60");
    put(0, 0, "3");
    expect(1, 3, "        90");

    reset();
    put(0, 0, "3");
    put(2, 0, "9");
    put(0, 1, "=sUm($A$1:a3)");
    put(1, 1, "=AVG(A1:A3)");
    put(2, 1, "=MIN(A1:A3)");
    put(3, 1, "=MAX(A1:A3)");
    put(4, 1, "=COUNT(A1:A3)");
    expect(0, 1, "        12");
    expect(1, 1, "         6");
    expect(2, 1, "         3");
    expect(3, 1, "         9");
    expect(4, 1, "         2");
    put(1, 0, "6"); /* A previously empty range cell is a dependency. */
    expect(0, 1, "        18");
    expect(1, 1, "         6");
    expect(4, 1, "         3");
    put(1, 0, "text");
    expect(0, 1, "        12");
    expect(4, 1, "         3");
    put(1, 0, "=Z99");
    expect(4, 1, "         3");
    put(98, 25, "20");
    expect(0, 1, "        32");
    put(98, 25, "30"); /* Dependency extends outside the scanned rectangle. */
    expect(0, 1, "        42");
    put(5, 1, "=SUM(A3:A1)");
    expect(5, 1, "         0");
    put(5, 1, "=AVG(A3:A1)");
    expect(5, 1, "  #ERR    ");

    reset();
    put(0, 0, "=B1");
    put(0, 1, "=A1");
    expect(0, 0, "  #CIRC   ");
    expect(0, 1, "  #CIRC   ");
    put(0, 1, "5");
    expect(0, 0, "         5");
    put(0, 1, "=COUNT(A1:A1)");
    expect(0, 1, "         1");
    expect(0, 0, "  #CIRC   "); /* A cached COUNT must not mask a cycle. */
    put(0, 2, "=B1+1");
    put(0, 0, "=C1");
    expect(0, 2, "         2");
    expect(0, 0, "  #CIRC   "); /* The COUNT restriction propagates to callers. */
    put(0, 1, "=SUM(A1:B1)");
    expect(0, 1, "  #CIRC   ");
    put(0, 1, "=1/0");
    expect(0, 1, "  #ERR    ");
    put(0, 1, "=#REF!+1");
    expect(0, 1, "  #REF!   ");

    reset();
    for (row = 0; row < 26; ++row) {
        snprintf(formula, sizeof(formula), "=A%d", row + 2);
        put(row, 0, formula);
    }
    put(26, 0, "1");
    expect(1, 0, "         1"); /* Warm a 25-formula chain. */
    expect(0, 0, "  #ERR    "); /* The 26th formula still exceeds the limit. */

    reset();
    put(0, 1, "=A1+1");
    put(0, 0, "=RAND()+B1");
    expect(0, 0, "         8");
    expect(0, 1, "         9"); /* No value from RAND's temporary empty A1. */
    put(0, 2, "=RAND(6)");
    expect(0, 2, "         1");

    /* UI structural edits move allocations and then flush the full cache. */
    cells[1][0] = cells[0][0];
    cells[0][0] = NULL;
    invalidate_formula_cache();
    put(1, 1, "=A2");
    expect(1, 1, "         8");
    reset();
    if (failures) {
        printf("%d calculator checks failed\n", failures);
        return 1;
    }
    puts("calculator checks passed");
    return 0;
}
