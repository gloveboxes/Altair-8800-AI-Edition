/* Silent redraw workloads; run with ntvcm -p to measure Z80 cycles. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SHEETS.H"

int dirty = 0;
unsigned svc_random_u16(void) { return 7; }

int main(int argc, char **argv)
{
    static const char *expected[] = {
        "     37034", "   1222155", "     16385", " 123456789",
        "     37034", "   9777240"
    };
    char formula[80];
    char rendered[CELL_WIDTH + 1];
    int scenario = argc > 1 ? atoi(argv[1]) : 0;
    int repetitions = argc > 2 ? atoi(argv[2]) : 1;
    int row;
    int iteration;

    if (scenario == 5) {
        for (row = 0; row < MAX_ROWS; ++row) {
            set_cell(row, 0, "12345");
            set_cell(row, 2, "12345");
        }
        set_cell(0, 1, "=SUM(A1:A99)");
        set_cell(2, 13, "=SUM(C1:C99)");
        set_cell(0, 3, "=B1+N3+B1+N3+B1+N3+B1+N3");
    } else if (scenario == 1) {
        for (row = 0; row < MAX_ROWS; ++row)
            set_cell(row, 0, "12345");
        for (row = 0; row < 20; ++row)
            set_cell(row, 1, "=SUM(A1:A99)");
    } else if (scenario == 2) {
        set_cell(0, 0, "1");
        for (row = 1; row < 15; ++row) {
            snprintf(formula, sizeof(formula), "=A%d+A%d", row, row);
            set_cell(row, 0, formula);
        }
        for (row = 0; row < 20; ++row)
            set_cell(row, 1, "=A15+1");
    } else if (scenario == 3) {
        set_cell(0, 0, "=123456789");
    } else {
        set_cell(0, 0, "12345");
        for (row = 0; row < 24; ++row)
            set_cell(row, 1, "=A1+A1*2-1");
    }

    for (iteration = 0; iteration < repetitions; ++iteration) {
        if (scenario == 4)
            set_cell(98, 25, iteration & 1 ? "2" : "3");
        else if (scenario != 3)
            invalidate_formula_cache();
        if (scenario == 5) {
            render_cell(0, 3, rendered);
        } else if (scenario == 3) {
            for (row = 0; row < 182; ++row)
                render_cell(0, 0, rendered);
        } else {
            for (row = 0; row < (scenario == 0 || scenario == 4 ? 24 : 20); ++row)
                render_cell(row, 1, rendered);
        }
    }
    if (strcmp(rendered, expected[scenario]) != 0) {
        printf("FAIL benchmark result [%s]\n", rendered);
        return 1;
    }
    puts(rendered);
    return 0;
}
