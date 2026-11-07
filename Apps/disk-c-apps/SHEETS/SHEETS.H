#ifndef SHEETS_H
#define SHEETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_COLUMNS 26
#define MAX_ROWS 99
#define CELL_WIDTH 10
#define VISIBLE_COLUMNS 7
#define VISIBLE_ROWS 26

/* Compatibility names retained while the UI module is migrated. */
#define MAXCOL MAX_COLUMNS
#define MAXROW MAX_ROWS
#define CWID CELL_WIDTH
#define VCOLS VISIBLE_COLUMNS
#define VROWS VISIBLE_ROWS

#define TITLE_ROW 1
#define HEADER_ROW 2
#define DATA_ROW 3
#define EDIT_ROW 29
#define STATUS_ROW 29
#define HELP_ROW 30

#define TITR TITLE_ROW
#define HDRR HEADER_ROW
#define DATR DATA_ROW
#define EDR EDIT_ROW
#define STR STATUS_ROW
#define HLPR HELP_ROW

#define ESC 27
#define CPMEOF 26
#define CR 13
#define LF 10
#define BKSP 8
#define CTLH 8
#define DEL 127
#define KUP 5
#define KDN 24
#define KRT 4
#define KLT 19
#define CTLC 3
#define CTLG 7
#define CTLK 11
#define CTLN 14
#define CTLO 15
#define CTLP 16
#define CTLQ 17
#define CTLR 18
#define CTLT 20
#define CTLV 22
#define CTLW 23
#define CTLU 21
#define CTLY 25

/* Create cell allocations through set_cell(): numeric text has a cached long
 * after its NUL. Structural edits may move or free whole allocations. */
extern char *cells[MAX_ROWS][MAX_COLUMNS];

#define CELL_AT(row, column) (cells[(row)][(column)])
extern int crow;
extern int ccol;
extern int trow;
extern int tcol;
extern char ebuf[80];
extern char ename[16];
extern char mesg[64];
extern char clip[80];
extern int hasclip;
extern int cliprow;
extern int clipcol;
extern int dirty;
extern int rall;

static inline bool is_ascii_digit(int character)
{
    return character >= '0' && character <= '9';
}

static inline bool is_letter(int character)
{
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z');
}

static inline int uppercase_ascii(int character)
{
    if (character >= 'a' && character <= 'z')
        return character - ('a' - 'A');
    return character;
}

int set_cell(int row, int column, const char *text);
void clear_cell(int row, int column);
void invalidate_formula_cache(void);
void render_cell(int row, int column, char *buffer);

#endif
