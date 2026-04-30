/**
 * @file vt100_terminal.c
 * @brief 60x30 text terminal for the ST7305 RLCD (400x300)
 *
 * Layout
 * ------
 * Display        : 400 x 300 px (ST7305 monochrome, white background)
 * Character grid : 66 columns x 30 rows
 * Cell size      : 6 x 10 px  (5px glyph + 1px gap; 66*6=396 px + 2px margins)
 * Left margin    :  2 px (2 px each side)
 * Glyph          : 5 x 7 px, embedded bitmap font, 1 px top offset in cell
 *
 * Thread safety
 * -------------
 * putchar() called from Core 1 (emulator task).
 * flush()   called from Core 0 (panel display task).
 * FreeRTOS mutex protects the character buffer and dirty-row bitmask.
 * flush() snapshots both under the mutex, then releases the lock before any
 * display I/O so the emulator is never stalled by DMA waits.
 */

#include "vt100_terminal.h"
#include "panel_display.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

/* ---- Layout constants -------------------------------------------------- */

#define CELL_W       6    /* pixels per column  (5px glyph + 1px gap)        */
#define CELL_H      10    /* pixels per row     (300 / 30)                   */
#define LEFT_MARGIN  2    /* 2 px each side: (400 - 66*6) / 2                */
#define GLYPH_XOFF   0    /* glyph left offset inside the 6 px cell          */
#define GLYPH_YOFF   1    /* glyph top  offset inside the 10 px cell         */
#define GLYPH_W      5    /* glyph pixel width                               */
#define GLYPH_H      7    /* glyph pixel height                              */

/* ---- Embedded 5x7 bitmap font ----------------------------------------- *
 *                                                                           *
 * 95 entries covering printable ASCII 0x20–0x7E.                           *
 * Each entry: 5 bytes, one per column, bit 0 = top row, bit 6 = bottom row.*
 * ------------------------------------------------------------------------ */
static const uint8_t s_font[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20  ' '  */
    {0x00,0x00,0x5F,0x00,0x00}, /* 0x21  '!'  */
    {0x00,0x07,0x00,0x07,0x00}, /* 0x22  '"'  */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 0x23  '#'  */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x24  '$'  */
    {0x23,0x13,0x08,0x64,0x62}, /* 0x25  '%'  */
    {0x36,0x49,0x55,0x22,0x50}, /* 0x26  '&'  */
    {0x00,0x05,0x03,0x00,0x00}, /* 0x27  '\'' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 0x28  '('  */
    {0x00,0x41,0x22,0x1C,0x00}, /* 0x29  ')'  */
    {0x14,0x08,0x3E,0x08,0x14}, /* 0x2A  '*'  */
    {0x08,0x08,0x3E,0x08,0x08}, /* 0x2B  '+'  */
    {0x00,0x50,0x30,0x00,0x00}, /* 0x2C  ','  */
    {0x08,0x08,0x08,0x08,0x08}, /* 0x2D  '-'  */
    {0x00,0x60,0x60,0x00,0x00}, /* 0x2E  '.'  */
    {0x20,0x10,0x08,0x04,0x02}, /* 0x2F  '/'  */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0x30  '0'  */
    {0x00,0x42,0x7F,0x40,0x00}, /* 0x31  '1'  */
    {0x42,0x61,0x51,0x49,0x46}, /* 0x32  '2'  */
    {0x21,0x41,0x45,0x4B,0x31}, /* 0x33  '3'  */
    {0x18,0x14,0x12,0x7F,0x10}, /* 0x34  '4'  */
    {0x27,0x45,0x45,0x45,0x39}, /* 0x35  '5'  */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 0x36  '6'  */
    {0x01,0x71,0x09,0x05,0x03}, /* 0x37  '7'  */
    {0x36,0x49,0x49,0x49,0x36}, /* 0x38  '8'  */
    {0x06,0x49,0x49,0x29,0x1E}, /* 0x39  '9'  */
    {0x00,0x36,0x36,0x00,0x00}, /* 0x3A  ':'  */
    {0x00,0x56,0x36,0x00,0x00}, /* 0x3B  ';'  */
    {0x08,0x14,0x22,0x41,0x00}, /* 0x3C  '<'  */
    {0x14,0x14,0x14,0x14,0x14}, /* 0x3D  '='  */
    {0x00,0x41,0x22,0x14,0x08}, /* 0x3E  '>'  */
    {0x02,0x01,0x51,0x09,0x06}, /* 0x3F  '?'  */
    {0x32,0x49,0x79,0x41,0x3E}, /* 0x40  '@'  */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 0x41  'A'  */
    {0x7F,0x49,0x49,0x49,0x36}, /* 0x42  'B'  */
    {0x3E,0x41,0x41,0x41,0x22}, /* 0x43  'C'  */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 0x44  'D'  */
    {0x7F,0x49,0x49,0x49,0x41}, /* 0x45  'E'  */
    {0x7F,0x09,0x09,0x09,0x01}, /* 0x46  'F'  */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 0x47  'G'  */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 0x48  'H'  */
    {0x00,0x41,0x7F,0x41,0x00}, /* 0x49  'I'  */
    {0x20,0x40,0x41,0x3F,0x01}, /* 0x4A  'J'  */
    {0x7F,0x08,0x14,0x22,0x41}, /* 0x4B  'K'  */
    {0x7F,0x40,0x40,0x40,0x40}, /* 0x4C  'L'  */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 0x4D  'M'  */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4E  'N'  */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 0x4F  'O'  */
    {0x7F,0x09,0x09,0x09,0x06}, /* 0x50  'P'  */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 0x51  'Q'  */
    {0x7F,0x09,0x19,0x29,0x46}, /* 0x52  'R'  */
    {0x46,0x49,0x49,0x49,0x31}, /* 0x53  'S'  */
    {0x01,0x01,0x7F,0x01,0x01}, /* 0x54  'T'  */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 0x55  'U'  */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 0x56  'V'  */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 0x57  'W'  */
    {0x63,0x14,0x08,0x14,0x63}, /* 0x58  'X'  */
    {0x07,0x08,0x70,0x08,0x07}, /* 0x59  'Y'  */
    {0x61,0x51,0x49,0x45,0x43}, /* 0x5A  'Z'  */
    {0x00,0x7F,0x41,0x41,0x00}, /* 0x5B  '['  */
    {0x02,0x04,0x08,0x10,0x20}, /* 0x5C  '\'  */
    {0x00,0x41,0x41,0x7F,0x00}, /* 0x5D  ']'  */
    {0x04,0x02,0x01,0x02,0x04}, /* 0x5E  '^'  */
    {0x40,0x40,0x40,0x40,0x40}, /* 0x5F  '_'  */
    {0x00,0x01,0x02,0x04,0x00}, /* 0x60  '`'  */
    {0x20,0x54,0x54,0x54,0x78}, /* 0x61  'a'  */
    {0x7F,0x48,0x44,0x44,0x38}, /* 0x62  'b'  */
    {0x38,0x44,0x44,0x44,0x20}, /* 0x63  'c'  */
    {0x38,0x44,0x44,0x48,0x7F}, /* 0x64  'd'  */
    {0x38,0x54,0x54,0x54,0x18}, /* 0x65  'e'  */
    {0x08,0x7E,0x09,0x01,0x02}, /* 0x66  'f'  */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 0x67  'g'  */
    {0x7F,0x08,0x04,0x04,0x78}, /* 0x68  'h'  */
    {0x00,0x44,0x7D,0x40,0x00}, /* 0x69  'i'  */
    {0x20,0x40,0x44,0x3D,0x00}, /* 0x6A  'j'  */
    {0x7F,0x10,0x28,0x44,0x00}, /* 0x6B  'k'  */
    {0x00,0x41,0x7F,0x40,0x00}, /* 0x6C  'l'  */
    {0x7C,0x04,0x18,0x04,0x78}, /* 0x6D  'm'  */
    {0x7C,0x08,0x04,0x04,0x78}, /* 0x6E  'n'  */
    {0x38,0x44,0x44,0x44,0x38}, /* 0x6F  'o'  */
    {0x7C,0x14,0x14,0x14,0x08}, /* 0x70  'p'  */
    {0x08,0x14,0x14,0x18,0x7C}, /* 0x71  'q'  */
    {0x7C,0x08,0x04,0x04,0x08}, /* 0x72  'r'  */
    {0x48,0x54,0x54,0x54,0x20}, /* 0x73  's'  */
    {0x04,0x3F,0x44,0x40,0x20}, /* 0x74  't'  */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 0x75  'u'  */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 0x76  'v'  */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 0x77  'w'  */
    {0x44,0x28,0x10,0x28,0x44}, /* 0x78  'x'  */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 0x79  'y'  */
    {0x44,0x64,0x54,0x4C,0x44}, /* 0x7A  'z'  */
    {0x00,0x08,0x36,0x41,0x00}, /* 0x7B  '{'  */
    {0x00,0x00,0x7F,0x00,0x00}, /* 0x7C  '|'  */
    {0x00,0x41,0x36,0x08,0x00}, /* 0x7D  '}'  */
    {0x10,0x08,0x08,0x10,0x08}, /* 0x7E  '~'  */
};

/* ---- Module state ------------------------------------------------------ */

static uint8_t           s_buffer[VT100_ROWS][VT100_COLS];
static uint32_t          s_dirty_rows;   /* bit i set → row i needs redraw  */
static int               s_col;
static int               s_row;
static bool              s_initialized;
static SemaphoreHandle_t s_mutex;

/* ---- VT100 escape-sequence state machine ------------------------------- */

typedef enum {
    VT_STATE_NORMAL = 0,
    VT_STATE_ESC,       /* received 0x1B, waiting for '[' or other          */
    VT_STATE_CSI,       /* received ESC[, collecting numeric parameters      */
} vt_state_t;

#define CSI_MAX_PARAMS  8

static vt_state_t s_vt_state;
static int        s_csi_params[CSI_MAX_PARAMS];
static int        s_csi_nparams;
static bool       s_csi_priv;   /* true when ESC[? prefix seen               */

/* ---- Internal helpers -------------------------------------------------- */

static void mark_all_dirty(void)
{
    /* 30 rows map to bits 0–29 of a uint32_t */
    s_dirty_rows = (1UL << VT100_ROWS) - 1UL;
}

/* Scroll the entire buffer up by one line, blank the last row. */
static void scroll_up(void)
{
    memmove(&s_buffer[0][0], &s_buffer[1][0],
            (size_t)(VT100_ROWS - 1) * VT100_COLS);
    memset(&s_buffer[VT100_ROWS - 1][0], ' ', VT100_COLS);
    mark_all_dirty();
}

/* Advance to the next line, scrolling if already on the last row. */
static void newline(void)
{
    s_row++;
    if (s_row >= VT100_ROWS) {
        s_row = VT100_ROWS - 1;
        scroll_up();
    }
}

/* Clamp a value to [lo, hi]. */
static inline int clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- CSI final-byte handler -------------------------------------------- */

static void handle_csi(uint8_t final)
{
    /* Convenience: first two params with defaults of 0 and 1 respectively. */
    int p0 = (s_csi_nparams > 0) ? s_csi_params[0] : 0;
    int p1 = (s_csi_nparams > 1) ? s_csi_params[1] : 0;

    switch (final) {

        /* ---- Cursor movement ------------------------------------------ */
        case 'A': /* cursor up */
            s_row = clamp(s_row - (p0 ? p0 : 1), 0, VT100_ROWS - 1);
            break;
        case 'B': /* cursor down */
            s_row = clamp(s_row + (p0 ? p0 : 1), 0, VT100_ROWS - 1);
            break;
        case 'C': /* cursor right */
            s_col = clamp(s_col + (p0 ? p0 : 1), 0, VT100_COLS - 1);
            break;
        case 'D': /* cursor left */
            s_col = clamp(s_col - (p0 ? p0 : 1), 0, VT100_COLS - 1);
            break;
        case 'E': /* cursor next line */
            s_row = clamp(s_row + (p0 ? p0 : 1), 0, VT100_ROWS - 1);
            s_col = 0;
            break;
        case 'F': /* cursor prev line */
            s_row = clamp(s_row - (p0 ? p0 : 1), 0, VT100_ROWS - 1);
            s_col = 0;
            break;
        case 'G': /* cursor horizontal absolute */
            s_col = clamp((p0 ? p0 : 1) - 1, 0, VT100_COLS - 1);
            break;
        case 'H': /* cursor position row;col (1-based) */
        case 'f':
            s_row = clamp((p0 ? p0 : 1) - 1, 0, VT100_ROWS - 1);
            s_col = clamp((p1 ? p1 : 1) - 1, 0, VT100_COLS - 1);
            break;
        case 'd': /* vertical line position absolute (1-based) */
            s_row = clamp((p0 ? p0 : 1) - 1, 0, VT100_ROWS - 1);
            break;

        /* ---- Erase in display ----------------------------------------- */
        case 'J':
            if (p0 == 2 || p0 == 3) {
                /* erase entire screen */
                memset(s_buffer, ' ', sizeof(s_buffer));
                mark_all_dirty();
            } else if (p0 == 1) {
                /* erase from start to cursor (inclusive) */
                for (int r = 0; r < s_row; r++) {
                    memset(s_buffer[r], ' ', VT100_COLS);
                    s_dirty_rows |= (1UL << r);
                }
                memset(s_buffer[s_row], ' ', (size_t)(s_col + 1));
                s_dirty_rows |= (1UL << s_row);
            } else {
                /* erase from cursor to end of screen (p0 == 0) */
                memset(s_buffer[s_row] + s_col, ' ',
                       (size_t)(VT100_COLS - s_col));
                s_dirty_rows |= (1UL << s_row);
                for (int r = s_row + 1; r < VT100_ROWS; r++) {
                    memset(s_buffer[r], ' ', VT100_COLS);
                    s_dirty_rows |= (1UL << r);
                }
            }
            break;

        /* ---- Erase in line -------------------------------------------- */
        case 'K':
            if (p0 == 2) {
                memset(s_buffer[s_row], ' ', VT100_COLS);
            } else if (p0 == 1) {
                memset(s_buffer[s_row], ' ', (size_t)(s_col + 1));
            } else {
                memset(s_buffer[s_row] + s_col, ' ',
                       (size_t)(VT100_COLS - s_col));
            }
            s_dirty_rows |= (1UL << s_row);
            break;

        /* ---- Scroll --------------------------------------------------- */
        case 'S': /* scroll up N lines */
        {
            int n = p0 ? p0 : 1;
            for (int i = 0; i < n; i++) scroll_up();
            break;
        }
        case 'T': /* scroll down N lines */
        {
            int n = p0 ? p0 : 1;
            for (int i = 0; i < n; i++) {
                memmove(&s_buffer[1][0], &s_buffer[0][0],
                        (size_t)(VT100_ROWS - 1) * VT100_COLS);
                memset(s_buffer[0], ' ', VT100_COLS);
            }
            mark_all_dirty();
            break;
        }

        /* ---- Insert / delete line ------------------------------------- */
        case 'L': /* insert N blank lines at cursor row */
        {
            int n = clamp(p0 ? p0 : 1, 1, VT100_ROWS - s_row);
            memmove(&s_buffer[s_row + n][0], &s_buffer[s_row][0],
                    (size_t)(VT100_ROWS - s_row - n) * VT100_COLS);
            for (int i = 0; i < n; i++)
                memset(s_buffer[s_row + i], ' ', VT100_COLS);
            mark_all_dirty();
            break;
        }
        case 'M': /* delete N lines at cursor row */
        {
            int n = clamp(p0 ? p0 : 1, 1, VT100_ROWS - s_row);
            memmove(&s_buffer[s_row][0], &s_buffer[s_row + n][0],
                    (size_t)(VT100_ROWS - s_row - n) * VT100_COLS);
            for (int i = VT100_ROWS - n; i < VT100_ROWS; i++)
                memset(s_buffer[i], ' ', VT100_COLS);
            mark_all_dirty();
            break;
        }

        /* ---- SGR / private / unrecognised ----------------------------- */
        case 'm': /* SGR – colours etc., ignored on monochrome */
        case 'h': /* mode set   – ignored */
        case 'l': /* mode reset – ignored */
        case 'r': /* DECSTBM scroll region – ignored */
        case 'n': /* DSR device status – ignored */
        default:
            break;
    }
}

/* ---- Public API -------------------------------------------------------- */

void vt100_terminal_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }

    memset(s_buffer, ' ', sizeof(s_buffer));
    s_col      = 0;
    s_row      = 0;
    s_vt_state = VT_STATE_NORMAL;
    s_csi_nparams = 0;
    s_csi_priv    = false;
    mark_all_dirty();
    s_initialized = true;

    /* Clear the physical display immediately so there is no stale content
     * visible before the first flush() call. */
    panel_display_fill_screen(PANEL_COLOR_WHITE);
    panel_display_present();
}

void vt100_terminal_putchar(uint8_t c)
{
    if (!s_initialized || !s_mutex) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    switch (s_vt_state) {

        /* ---- Normal text / C0 controls -------------------------------- */
        case VT_STATE_NORMAL:
            switch (c) {
                case 0x1B: /* ESC – start of escape sequence */
                    s_vt_state = VT_STATE_ESC;
                    break;
                case '\r': /* CR – move to column 0 */
                    s_col = 0;
                    break;
                case '\n': /* LF – move down one row */
                    newline();
                    break;
                case '\b': /* BS – erase previous character */
                    if (s_col > 0) {
                        s_col--;
                        s_buffer[s_row][s_col] = ' ';
                        s_dirty_rows |= (1UL << s_row);
                    }
                    break;
                case 0x07: /* BEL – ignore */
                case 0x00: /* NUL – ignore */
                    break;
                default:
                    if (c >= 0x20 && c <= 0x7E) {
                        s_buffer[s_row][s_col] = c;
                        s_dirty_rows |= (1UL << s_row);
                        s_col++;
                        if (s_col >= VT100_COLS) {
                            s_col = 0;
                            newline();
                        }
                    }
                    /* Other C1 / undefined bytes silently discarded */
                    break;
            }
            break;

        /* ---- ESC received, waiting for sequence type byte ------------- */
        case VT_STATE_ESC:
            switch (c) {
                case '[': /* CSI – Control Sequence Introducer */
                    s_vt_state    = VT_STATE_CSI;
                    s_csi_nparams = 0;
                    s_csi_priv    = false;
                    memset(s_csi_params, 0, sizeof(s_csi_params));
                    break;
                case 'c': /* RIS – reset to initial state */
                    memset(s_buffer, ' ', sizeof(s_buffer));
                    s_col = 0;
                    s_row = 0;
                    mark_all_dirty();
                    s_vt_state = VT_STATE_NORMAL;
                    break;
                case 'M': /* RI – reverse index (scroll down) */
                    if (s_row > 0) {
                        s_row--;
                    } else {
                        memmove(&s_buffer[1][0], &s_buffer[0][0],
                                (size_t)(VT100_ROWS - 1) * VT100_COLS);
                        memset(s_buffer[0], ' ', VT100_COLS);
                        mark_all_dirty();
                    }
                    s_vt_state = VT_STATE_NORMAL;
                    break;
                default:
                    /* Unknown ESC sequence – absorb this byte and return */
                    s_vt_state = VT_STATE_NORMAL;
                    break;
            }
            break;

        /* ---- CSI: collecting parameters and final byte ---------------- */
        case VT_STATE_CSI:
            if (c == '?') {
                /* Private-mode prefix (e.g. ESC[?25h cursor visibility) */
                s_csi_priv = true;
            } else if (c >= '0' && c <= '9') {
                /* Accumulate digit into current parameter */
                if (s_csi_nparams == 0) s_csi_nparams = 1;
                s_csi_params[s_csi_nparams - 1] =
                    s_csi_params[s_csi_nparams - 1] * 10 + (c - '0');
            } else if (c == ';') {
                /* Parameter separator – advance to next slot */
                if (s_csi_nparams < CSI_MAX_PARAMS) {
                    s_csi_nparams++;
                    s_csi_params[s_csi_nparams - 1] = 0;
                }
            } else if (c >= 0x40 && c <= 0x7E) {
                /* Final byte – dispatch and return to normal */
                if (s_csi_nparams == 0 && s_csi_params[0] != 0)
                    s_csi_nparams = 1;
                handle_csi(c);
                s_vt_state = VT_STATE_NORMAL;
            } else {
                /* Unexpected byte inside CSI – abort sequence */
                s_vt_state = VT_STATE_NORMAL;
            }
            break;
    }

    xSemaphoreGive(s_mutex);
}

void vt100_terminal_flush(void)
{
    if (!s_initialized || !s_mutex) {
        return;
    }

    /* ------------------------------------------------------------------ *
     * Snapshot both the dirty mask and the character buffer under the     *
     * mutex, then release the lock before touching the display hardware.  *
     * ------------------------------------------------------------------ */
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint32_t dirty = s_dirty_rows;
    s_dirty_rows   = 0;

    uint8_t snapshot[VT100_ROWS][VT100_COLS];
    if (dirty) {
        memcpy(snapshot, s_buffer, sizeof(s_buffer));
    }

    xSemaphoreGive(s_mutex);

    if (!dirty) {
        return;
    }

    /* Render each dirty row -------------------------------------------- */
    for (int row = 0; row < VT100_ROWS; row++) {
        if (!(dirty & (1UL << row))) {
            continue;
        }

        int y_base = row * CELL_H;

        /* Clear the full display-width band (includes both margins). */
        panel_display_fill_rect(0, y_base, panel_display_width(), CELL_H,
                                PANEL_COLOR_WHITE);

        /* Render each non-space glyph using the embedded 5x7 font. */
        for (int col = 0; col < VT100_COLS; col++) {
            uint8_t ch = snapshot[row][col];
            if (ch < 0x21 || ch > 0x7E) {
                continue;  /* skip space and non-printable */
            }

            int gidx = ch - 0x20;
            int gx   = LEFT_MARGIN + col * CELL_W + GLYPH_XOFF;
            int gy   = y_base + GLYPH_YOFF;

            for (int fc = 0; fc < GLYPH_W; fc++) {
                uint8_t col_data = s_font[gidx][fc];
                for (int fr = 0; fr < GLYPH_H; fr++) {
                    if (col_data & (1u << fr)) {
                        panel_display_draw_pixel(gx + fc, gy + fr,
                                                 PANEL_COLOR_BLACK);
                    }
                }
            }
        }
    }

    /* Push the updated framebuffer region to the physical display. */
    panel_display_present();
}
