/**
 * @file vt100_terminal.h
 * @brief Simple 40x25 text terminal for the ST7305 RLCD display
 *
 * Renders emulator output as a 66-column by 30-row text terminal on the
 * ST7305 400x300 monochrome display.  Each character cell is 6x10 pixels
 * (5x7 glyph + 1px inter-character gap).  66*6=396 px, 2 px margin each
 * side; 30*10=300 px exact.
 *
 * Thread-safe split: putchar() is called from Core 1 (emulator task),
 * flush() is called from Core 0 (panel display task).
 */

#ifndef VT100_TERMINAL_H
#define VT100_TERMINAL_H

#include <stdint.h>

#define VT100_COLS  66
#define VT100_ROWS  30

/**
 * @brief Initialise the VT100 terminal.
 *
 * Clears the character buffer, resets the cursor to (0,0), clears the
 * physical display to white, and marks every row dirty so the first
 * flush() call renders the blank terminal.
 *
 * Must be called after panel_display_init().
 */
void vt100_terminal_init(void);

/**
 * @brief Write one character to the terminal.
 *
 * Handles:
 *   - Printable ASCII 0x20–0x7E  – place glyph, advance cursor, wrap/scroll
 *   - CR  (0x0D) – move cursor to column 0
 *   - LF  (0x0A) – move cursor down one row, scroll if at bottom
 *   - BS  (0x08) – move cursor left one column and erase the vacated cell
 *   - All other bytes are silently discarded.
 *
 * Called from Core 1 (emulator).  Thread-safe.
 *
 * @param c Character byte to write
 */
void vt100_terminal_putchar(uint8_t c);

/**
 * @brief Flush dirty rows to the ST7305 display.
 *
 * Renders every row that has been modified since the last flush, then
 * calls panel_display_present() to push the updated framebuffer region
 * to the hardware.  No-ops when there is nothing to redraw.
 *
 * Called from Core 0 (panel display task).  Thread-safe.
 */
void vt100_terminal_flush(void);

#endif /* VT100_TERMINAL_H */
