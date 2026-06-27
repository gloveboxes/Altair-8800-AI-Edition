#include <stdio.h>
#include <string.h>

/*
 * MANC89.C - Mancala / African Bean Game for CP/M 2.2 on the Z80.
 *
 * dcc C89 port of the BDS C MANCALA.C app.  VT100/xterm.js display,
 * 30 rows by 80 columns, player vs computer, Kalah-style rules with an
 * alpha-beta search engine (Easy/Medium/Hard).
 *
 * Console output is fully buffered through stdout.  A single 8 KB buffer
 * is installed once at startup with setvbuf(); fflush() drains it at the
 * points where the player needs to see the board (before input and during
 * the computer's "thinking" pause).  This keeps the many small VT100 writes
 * fast instead of issuing one BDOS call per character.
 *
 * Build from the dcc repo with this file as manc89.c (bump the C stack so
 * the search recursion has room):
 *   DCC_STACK_SIZE=8192 ./ma.sh manc89 peep   -> MANC89.COM
 *   ntvcm MANC89
 *
 * Keys:
 *   E/M/H              Easy / Medium / Hard
 *   Left/Right arrows  Select pit
 *   1-6                Select pit
 *   Space or Return    Sow selected pit
 *   U                  Undo last player move
 *   ? or T             Hint
 *   Q, ESC, Ctrl-C     Quit
 */

#define PITS 6
#define TOT 14
#define HST 6
#define CST 13

#define SCRW 80
#define SCRH 30

#define BROW 7
#define BCOL 18
#define PCW 7
#define PCH 4
#define PNLT 5
#define PNLL 5
#define PNLW 72
#define PNLH 21

#define ESC 27
#define XK_LT 19
#define XK_RT 4
#define XK_SPC 32
#define XK_CC 3

#define KEYQ 81
#define KEYq 113
#define KEYR 13

#define KNON 0
#define KLT 1
#define KRT 2
#define KPLC 3
#define KQUI 4

#define CBUF 8192

extern int bdos(int func, int val);
extern int inp(unsigned port);
extern void outp(unsigned port, unsigned val);

/* Single console buffer, installed once at startup. */
static char console_buffer[CBUF];

static int pit[TOT];
static int undo_pit[TOT];
static int selected;
static int undo_selected;
static int quit;
static int game_over;
static int esc_state;
static int level;
static int undo_ok;

/* --- low level console helpers -------------------------------------- */

/* flush() - Drain the buffered console output. */
static void flush(void)
{
    fflush(stdout);
}

/* put_str(s) - Print a NUL-terminated string. */
static void put_str(char *s)
{
    while (*s)
        putchar(*s++);
}

/* put_num(n) - Print a signed integer in decimal. */
static void put_num(int n)
{
    char buf[6];
    int i;

    if (n == 0) {
        putchar('0');
        return;
    }
    if (n < 0) {
        putchar('-');
        n = -n;
    }
    i = 0;
    while (n > 0 && i < 6) {
        buf[i++] = (char)((n % 10) + '0');
        n /= 10;
    }
    while (i--)
        putchar(buf[i]);
}

/* put_num2col(n) - Print a right-aligned two-column number. */
static void put_num2col(int n)
{
    if (n < 10)
        putchar(' ');
    put_num(n);
}

/* cursor_move(r,c) - Move cursor to 1-based row/column. */
static void cursor_move(int r, int c)
{
    putchar(ESC);
    putchar('[');
    put_num(r);
    putchar(';');
    put_num(c);
    putchar('H');
}

/* set_color(n) - Emit ESC[Nm for any SGR code (fg, bg, attr). */
static void set_color(int n)
{
    putchar(ESC);
    putchar('[');
    put_num(n);
    putchar('m');
}

/* reset_color() - Reset all attributes. */
static void reset_color(void)
{
    put_str("\033[0m");
}

/* clear_screen() - Clear screen, reset attributes, home cursor. */
static void clear_screen(void)
{
    put_str("\033[2J\033[0m");
    cursor_move(1, 1);
}

/* hide_cursor() - Hide the terminal cursor. */
static void hide_cursor(void)
{
    put_str("\033[?25l");
}

/* show_cursor() - Show the terminal cursor. */
static void show_cursor(void)
{
    put_str("\033[?25h");
}

/* erase_eol() - Erase from cursor to end of line. */
static void erase_eol(void)
{
    put_str("\033[K");
}

/* key_read() - Flush the screen, then read one key without waiting. */
static int key_read(void)
{
    flush();
    return bdos(6, 0xFF) & 0xFF;
}

/* random16() - Read a 16-bit random number from the emulator RNG port. */
static unsigned random16(void)
{
    unsigned r;

    outp(45, 1);
    r = inp(200);
    r |= (unsigned)(inp(200) << 8);
    return r;
}

/* --- forward declarations -------------------------------------------- */

static int show_hint(void);
static int redraw(int i);
static int draw_pit(int i, int hi);
static int update_pit(int i);
static int update_store(int i);
static int board_has_move(int b[], int p);
static int board_collect_endgame(int b[]);
static int alpha_beta(int b[], int p, int dep, int alp, int bet, int ord);
static int move_order_score(int b[], int i, int p);
static int order_moves(int b[], int p, int mv[]);

/* --- board drawing --------------------------------------------------- */

/* wood_color() - Select the board's dark wood color. */
static int wood_color(void)
{
    set_color(44);
    set_color(97);
    return 0;
}

/* hole_color() - Select recessed pit color. */
static int hole_color(void)
{
    set_color(41);
    set_color(97);
    return 0;
}

/* checker_color(r,c) - African-inspired checker border color. */
static int checker_color(int r, int c)
{
    if (((r / 2) + (c / 2)) & 1) {
        if (r & 1)
            return 42;
        return 43;
    }
    if (c & 1)
        return 41;
    return 44;
}

/* draw_border() - Draw the checker outline. */
static int draw_border(void)
{
    int r;
    int c;

    for (c = 0; c < SCRW; c += 2) {
        set_color(checker_color(0, c / 2));
        cursor_move(2, c + 1);
        putchar(' ');
        putchar(' ');
        set_color(checker_color(SCRH - 1, c / 2));
        cursor_move(SCRH, c + 1);
        putchar(' ');
        putchar(' ');
    }
    for (r = 3; r < SCRH; r++) {
        set_color(checker_color(r - 2, 0));
        cursor_move(r, 1);
        putchar(' ');
        putchar(' ');
        set_color(checker_color(r - 2, (SCRW / 2) - 1));
        cursor_move(r, SCRW - 1);
        putchar(' ');
        putchar(' ');
    }
    reset_color();
    return 0;
}

/* draw_status() - Draw score stores and turn. */
static int draw_status(void)
{
    reset_color();
    set_color(37);
    cursor_move(1, 3);
    put_str("MANCALA - THE AFRICAN BEAN GAME");
    cursor_move(1, 49);
    put_str("YOU:");
    put_num2col(pit[HST]);
    put_str("  CPU:");
    put_num2col(pit[CST]);
    put_str("  ");
    if (level == 0)
        put_str("E:EASY");
    else if (level == 1)
        put_str("M:MED");
    else
        put_str("H:HARD");
    erase_eol();
    return 0;
}

/* pit_col(i) - Screen column for pit i. */
static int pit_col(int i)
{
    if (i < HST)
        return BCOL + (i * (PCW + 1));
    return BCOL + ((12 - i) * (PCW + 1));
}

/* pit_row(i) - Screen row for pit i. */
static int pit_row(int i)
{
    if (i < HST)
        return BROW + 10;
    return BROW + 1;
}

/* draw_beans(n) - Draw bean dots for a pit. */
static int draw_beans(int n)
{
    int i;
    int m;

    m = n;
    if (m > 5)
        m = 5;
    for (i = 0; i < m; i++)
        putchar('o');
    for (; i < 6; i++)
        putchar(' ');
    return 0;
}

/* draw_pit(i,hi) - Draw one small pit. */
static int draw_pit(int i, int hi)
{
    int r;
    int c;
    int row;

    r = pit_row(i);
    c = pit_col(i);
    for (row = 0; row < PCH; row++) {
        cursor_move(r + row, c);
        if (hi) {
            set_color(43);
            set_color(30);
        } else {
            hole_color();
        }
        if (row == 0 || row == PCH - 1) {
            put_str("       ");
        } else if (row == 1) {
            putchar(' ');
            put_num2col(pit[i]);
            put_str("    ");
        } else {
            putchar(' ');
            if (pit[i] > 0)
                draw_beans(pit[i]);
            else
                put_str("      ");
        }
        reset_color();
    }
    return 0;
}

/* update_pit(i) - Update only the changing rows of one small pit. */
static int update_pit(int i)
{
    int r;
    int c;
    int hi;

    r = pit_row(i);
    c = pit_col(i);
    hi = (i < HST && i == selected);

    cursor_move(r + 1, c);
    if (hi) {
        set_color(43);
        set_color(30);
    } else {
        hole_color();
    }
    putchar(' ');
    put_num2col(pit[i]);
    put_str("    ");

    cursor_move(r + 2, c);
    if (hi) {
        set_color(43);
        set_color(30);
    } else {
        hole_color();
    }
    putchar(' ');
    if (pit[i] > 0)
        draw_beans(pit[i]);
    else
        put_str("      ");
    reset_color();
    return 0;
}

/* draw_store(i) - Draw one store. */
static int draw_store(int i)
{
    int r;
    int c;
    int row;

    if (i == HST)
        c = 67;
    else
        c = 8;
    r = BROW + 4;
    for (row = 0; row < 8; row++) {
        cursor_move(r + row, c);
        hole_color();
        if (row == 0 || row == 7) {
            put_str("       ");
        } else if (row == 2) {
            put_str("  ");
            put_num2col(pit[i]);
            put_str("   ");
        } else if (row == 4) {
            putchar(' ');
            if (i == HST)
                put_str("YOU ");
            else
                put_str("CPU ");
            put_str("  ");
        } else {
            put_str("       ");
        }
        reset_color();
    }
    return 0;
}

/* update_store(i) - Update only the changing count row of one store. */
static int update_store(int i)
{
    int r;
    int c;

    if (i == HST)
        c = 67;
    else
        c = 8;
    r = BROW + 4;
    cursor_move(r + 2, c);
    hole_color();
    put_str("  ");
    put_num2col(pit[i]);
    put_str("   ");
    reset_color();
    return 0;
}

/* clear_note_row(r) - Clear a message row without touching the border. */
static int clear_note_row(int r)
{
    int i;

    reset_color();
    cursor_move(r, 5);
    for (i = 0; i < 72; i++)
        putchar(' ');
    return 0;
}

/* draw_panel() - Draw the carved wooden board panel. */
static int draw_panel(void)
{
    int r;
    int c;

    for (r = 0; r < PNLH; r++) {
        cursor_move(PNLT + r, PNLL);
        wood_color();
        for (c = 0; c < PNLW; c++)
            putchar(' ');
        reset_color();
    }
    for (r = PNLT + 3; r < PNLT + PNLH - 2; r += 4) {
        cursor_move(r, PNLL + 4);
        wood_color();
        set_color(33);
        put_str("................................................................");
        reset_color();
    }
    return 0;
}

/* draw_labels() - Draw pit labels. */
static int draw_labels(void)
{
    int i;

    reset_color();
    wood_color();
    set_color(93);
    cursor_move(6, 30);
    put_str("COMPUTER SIDE");
    reset_color();
    wood_color();
    set_color(93);
    cursor_move(24, 33);
    put_str("YOUR SIDE");
    for (i = 0; i < PITS; i++) {
        wood_color();
        set_color(93);
        cursor_move(BROW + 15, pit_col(i) + 3);
        putchar('1' + i);
        reset_color();
    }
    reset_color();
    return 0;
}

/* note(s) - Draw a status message. */
static int note(char *s)
{
    clear_note_row(27);
    set_color(37);
    cursor_move(27, 5);
    put_str(s);
    reset_color();
    flush();
    return 0;
}

/* draw_help() - Draw the persistent shortcut keys. */
static int draw_help(void)
{
    clear_note_row(28);
    set_color(37);
    cursor_move(28, 5);
    put_str("KEYS: 1-6 SELECT  SPACE SOW  U UNDO  E/M/H LEVEL  ? HINT  Q QUIT");
    reset_color();
    return 0;
}

/* draw_all() - Draw the whole board and pits. */
static int draw_all(void)
{
    int i;

    clear_screen();
    hide_cursor();
    draw_border();
    draw_status();
    draw_panel();
    draw_labels();
    draw_store(CST);
    draw_store(HST);
    for (i = 0; i < HST; i++)
        draw_pit(i, i == selected);
    for (i = 7; i < CST; i++)
        draw_pit(i, 0);
    note("YOUR TURN: CHOOSE A PIT WITH BEANS");
    draw_help();
    return 0;
}

/* draw_positions() - Redraw pit and store positions only. */
static int draw_positions(void)
{
    int i;

    draw_status();
    draw_store(CST);
    draw_store(HST);
    for (i = 0; i < HST; i++)
        draw_pit(i, i == selected);
    for (i = 7; i < CST; i++)
        draw_pit(i, 0);
    return 0;
}

/* redraw(i) - Redraw a pit or store by index. */
static int redraw(int i)
{
    if (i == HST || i == CST)
        update_store(i);
    else
        update_pit(i);
    return 0;
}

/* --- game state ------------------------------------------------------ */

/* init() - Initialize game state. */
static int init(void)
{
    int i;

    for (i = 0; i < TOT; i++)
        pit[i] = 4;
    pit[HST] = 0;
    pit[CST] = 0;
    selected = 0;
    quit = 0;
    game_over = 0;
    esc_state = 0;
    level = 1;
    undo_ok = 0;
    return 0;
}

/* keymap(c) - Decode raw or translated control keys. */
static int keymap(int c)
{
    if (esc_state == 2) {
        if (c == 0)
            return KNON;
        esc_state = 0;
        if (c == 'C')
            return KRT;
        if (c == 'D')
            return KLT;
        return KNON;
    }
    if (esc_state == 1) {
        if (c == 0)
            return KNON;
        esc_state = 0;
        if (c == '[') {
            esc_state = 2;
            return KNON;
        }
        return KQUI;
    }
    if (c == 0)
        return KNON;
    if (c == ESC) {
        esc_state = 1;
        return KNON;
    }
    if (c == XK_CC || c == KEYQ || c == KEYq)
        return KQUI;
    if (c == XK_LT)
        return KLT;
    if (c == XK_RT)
        return KRT;
    if (c == XK_SPC || c == KEYR)
        return KPLC;
    return KNON;
}

/* collect_endgame() - Collect remaining beans at game end. */
static int collect_endgame(void)
{
    board_collect_endgame(pit);
    game_over = 1;
    return 0;
}

/* opposite_pit(i) - Return the opposite pit index. */
static int opposite_pit(int i)
{
    return 12 - i;
}

/* owns_pit(i,p) - Test whether pit i belongs to player p. */
static int owns_pit(int i, int p)
{
    if (p == 0 && i >= 0 && i < HST)
        return 1;
    if (p == 1 && i > HST && i < CST)
        return 1;
    return 0;
}

/* store_index(p) - Return the store index for player p. */
static int store_index(int p)
{
    if (p == 0)
        return HST;
    return CST;
}

/* is_opponent_store(i,p) - Test whether index i is the opponent store. */
static int is_opponent_store(int i, int p)
{
    if (p == 0 && i == CST)
        return 1;
    if (p == 1 && i == HST)
        return 1;
    return 0;
}

/* play_move(i,p) - Sow from pit i for player p (with display). */
static int play_move(int i, int p)
{
    int cnt;
    int pos;
    int op;
    int st;

    if (!owns_pit(i, p) || pit[i] == 0)
        return 0;
    cnt = pit[i];
    pit[i] = 0;
    redraw(i);
    pos = i;
    while (cnt > 0) {
        pos++;
        if (pos >= TOT)
            pos = 0;
        if (!is_opponent_store(pos, p)) {
            pit[pos]++;
            cnt--;
            redraw(pos);
            if (pos == HST || pos == CST)
                draw_status();
        }
    }

    st = store_index(p);
    if (owns_pit(pos, p) && pit[pos] == 1) {
        op = opposite_pit(pos);
        if (pit[op] > 0) {
            pit[st] += pit[op] + 1;
            pit[op] = 0;
            pit[pos] = 0;
            redraw(op);
            redraw(pos);
            redraw(st);
            draw_status();
            if (p == 0)
                note("CAPTURE! THE OPPOSITE PIT FALLS TO YOU");
            else
                note("COMPUTER CAPTURES FROM THE OPPOSITE PIT");
        }
    }

    if (!board_has_move(pit, 0) || !board_has_move(pit, 1)) {
        collect_endgame();
        draw_positions();
        return 0;
    }
    draw_status();
    if (pos == st)
        return 2;
    return 1;
}

/* has_legal_move(p) - Return non-zero if player p has a move. */
static int has_legal_move(int p)
{
    return board_has_move(pit, p);
}

/* --- simulated board (search) ---------------------------------------- */

/* board_copy(dst,src) - Copy a simulated board (Z80 LDIR block copy). */
static void board_copy(int dst[], int src[])
{
    memcpy(dst, src, TOT * sizeof(int));
}

/* board_has_move(b,p) - Test for a simulated legal move. */
static int board_has_move(int b[], int p)
{
    int i;

    if (p == 0) {
        for (i = 0; i < HST; i++)
            if (b[i] > 0)
                return 1;
    } else {
        for (i = 7; i < CST; i++)
            if (b[i] > 0)
                return 1;
    }
    return 0;
}

/* board_collect_endgame(b) - Sweep simulated end-game beans. */
static int board_collect_endgame(int b[])
{
    int i;

    for (i = 0; i < HST; i++) {
        b[HST] += b[i];
        b[i] = 0;
    }
    for (i = 7; i < CST; i++) {
        b[CST] += b[i];
        b[i] = 0;
    }
    return 0;
}

/* board_sow(b,i,p) - Sow on a simulated board. */
static int board_sow(int b[], int i, int p)
{
    int cnt;
    int pos;
    int op;
    int st;
    int os;

    if (!owns_pit(i, p) || b[i] == 0)
        return 0;
    /* Hand-inlined store_index()/is_opponent_store(): dcc does not inline, and
     * this loop runs once per sown bean at every search node.  st = own store,
     * os = the opponent store that sowing must skip. */
    if (p == 0) {
        st = HST;
        os = CST;
    } else {
        st = CST;
        os = HST;
    }
    cnt = b[i];
    b[i] = 0;
    pos = i;
    while (cnt > 0) {
        pos++;
        if (pos >= TOT)
            pos = 0;
        if (pos != os) {
            b[pos]++;
            cnt--;
        }
    }

    /* Capture: last bean lands in an own, previously-empty, non-store pit. */
    if (pos != st && b[pos] == 1 &&
        ((p == 0 && pos < HST) || (p == 1 && pos > HST && pos < CST))) {
        op = 12 - pos;
        if (b[op] > 0) {
            b[st] += b[op] + 1;
            b[op] = 0;
            b[pos] = 0;
        }
    }
    if (!board_has_move(b, 0) || !board_has_move(b, 1)) {
        board_collect_endgame(b);
        return 0;
    }
    if (pos == st)
        return 2;
    return 1;
}

/* int_abs(n) - Return the absolute value. */
static int int_abs(int n)
{
    if (n < 0)
        return -n;
    return n;
}

/* move_order_score(b,i,p) - Immediate move-ordering score. */
static int move_order_score(int b[], int i, int p)
{
    int tb[TOT];
    int st;
    int old;
    int res;
    int gain;
    int dif;
    int val;

    if (!owns_pit(i, p) || b[i] == 0)
        return -30000;
    board_copy(tb, b);
    st = store_index(p);
    old = tb[st];
    res = board_sow(tb, i, p);
    gain = tb[st] - old;
    val = (gain * 100) + b[i];
    if (res == 2)
        val += 60;
    if (res == 0) {
        dif = tb[CST] - tb[HST];
        val += int_abs(dif) * 20;
    }
    return val;
}

/* board_score(b) - Evaluate a simulated board for the computer. */
static int board_score(int b[])
{
    int i;
    int hs;
    int cs;
    int hm;
    int cm;
    int cdif;
    int pdif;
    int mdif;

    hs = 0;
    cs = 0;
    hm = 0;
    cm = 0;
    for (i = 0; i < HST; i++) {
        hs += b[i];
        if (b[i] > 0)
            hm++;
    }
    for (i = 7; i < CST; i++) {
        cs += b[i];
        if (b[i] > 0)
            cm++;
    }
    if (hs == 0 || cs == 0)
        return ((b[CST] + cs) - (b[HST] + hs)) * 1000;
    cdif = b[CST] - b[HST];
    pdif = cs - hs;
    mdif = cm - hm;
    return (cdif * 100) + (pdif * 4) + (mdif * 3);
}

/* order_moves(b,p,mv) - Build a tactical move order. */
static int order_moves(int b[], int p, int mv[])
{
    int i;
    int j;
    int n;
    int lo;
    int hi;
    int tmp;
    int va[6];

    n = 0;
    if (p == 0) {
        lo = 0;
        hi = HST;
    } else {
        lo = 7;
        hi = CST;
    }
    for (i = lo; i < hi; i++) {
        if (b[i] > 0) {
            mv[n] = i;
            va[n] = move_order_score(b, i, p);
            n++;
        }
    }
    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            if ((p == 1 && va[j] > va[i]) || (p == 0 && va[j] < va[i])) {
                tmp = va[i];
                va[i] = va[j];
                va[j] = tmp;
                tmp = mv[i];
                mv[i] = mv[j];
                mv[j] = tmp;
            }
        }
    }
    return n;
}

/* alpha_beta() - Alpha-beta score for simulated play. */
static int alpha_beta(int b[], int p, int dep, int alp, int bet, int ord)
{
    int i;
    int n;
    int res;
    int val;
    int best;
    int mv[6];
    int nb[TOT];

    if (dep <= 0)
        return board_score(b);
    if (!board_has_move(b, p))
        return board_score(b);
    /* Order moves only at upper nodes.  At dep==1 the children are leaves, so
     * order_moves()'s per-move board_copy+board_sow costs more than the pruning
     * it buys. */
    if (ord && dep > 1) {
        n = order_moves(b, p, mv);
    } else {
        n = 0;
        if (p == 0) {
            for (i = 0; i < HST; i++)
                if (b[i] > 0)
                    mv[n++] = i;
        } else {
            for (i = 7; i < CST; i++)
                if (b[i] > 0)
                    mv[n++] = i;
        }
    }

    if (p == 1)
        best = -30000;
    else
        best = 30000;

    if (p == 0) {
        for (i = 0; i < n; i++) {
            board_copy(nb, b);
            res = board_sow(nb, mv[i], p);
            if (res == 2)
                val = alpha_beta(nb, p, dep - 1, alp, bet, ord);
            else
                val = alpha_beta(nb, 1, dep - 1, alp, bet, ord);
            if (val < best)
                best = val;
            if (best < bet)
                bet = best;
            if (alp >= bet)
                return best;
        }
    } else {
        for (i = 0; i < n; i++) {
            board_copy(nb, b);
            res = board_sow(nb, mv[i], p);
            if (res == 2)
                val = alpha_beta(nb, p, dep - 1, alp, bet, ord);
            else
                val = alpha_beta(nb, 0, dep - 1, alp, bet, ord);
            if (val > best)
                best = val;
            if (best > alp)
                alp = best;
            if (alp >= bet)
                return best;
        }
    }
    return best;
}

/* random_pick() - Pick a random legal computer pit. */
static int random_pick(void)
{
    int i;
    int n;
    int pick;

    n = 0;
    for (i = 7; i < CST; i++)
        if (pit[i] > 0)
            n++;
    if (n == 0)
        return 7;
    pick = (int)(random16() % n);
    for (i = 7; i < CST; i++) {
        if (pit[i] > 0) {
            if (pick == 0)
                return i;
            pick--;
        }
    }
    return 7;
}

/* --- selection / level / undo ---------------------------------------- */

/* step_select(d) - Move the selected human pit. */
static int step_select(int d)
{
    int old;

    old = selected;
    selected += d;
    if (selected < 0)
        selected = PITS - 1;
    if (selected >= PITS)
        selected = 0;
    if (old == selected)
        return 0;
    draw_pit(old, 0);
    draw_pit(selected, 1);
    return 0;
}

/* set_select(n) - Select a human pit by number. */
static int set_select(int n)
{
    int old;

    if (n < 0 || n >= PITS)
        return 0;
    old = selected;
    selected = n;
    if (old == selected)
        return 0;
    draw_pit(old, 0);
    draw_pit(selected, 1);
    return 0;
}

/* set_level(n) - Change the computer level during play. */
static int set_level(int n)
{
    if (n > 2)
        n = 2;
    level = n;
    draw_status();
    if (level == 0)
        note("COMPUTER LEVEL E: EASY");
    else if (level == 1)
        note("COMPUTER LEVEL M: MEDIUM");
    else
        note("COMPUTER LEVEL H: HARD");
    return 0;
}

/* save_undo() - Save state before a player move. */
static int save_undo(void)
{
    int i;

    for (i = 0; i < TOT; i++)
        undo_pit[i] = pit[i];
    undo_selected = selected;
    undo_ok = 1;
    return 0;
}

/* restore_undo() - Restore the saved player move. */
static int restore_undo(void)
{
    int i;

    if (!undo_ok) {
        note("NOTHING TO UNDO");
        return 0;
    }
    for (i = 0; i < TOT; i++)
        pit[i] = undo_pit[i];
    selected = undo_selected;
    game_over = 0;
    esc_state = 0;
    undo_ok = 0;
    draw_positions();
    note("LAST MOVE UNDONE");
    return 0;
}

/* --- turn drivers ---------------------------------------------------- */

/* human() - Process the human turn. */
static int human(void)
{
    int k;
    int c;
    int res;

    note("YOUR TURN: CHOOSE A PIT WITH BEANS");
    while (!quit && !game_over) {
        c = key_read();
        k = keymap(c);
        if (c >= '1' && c <= '6') {
            set_select(c - '1');
            continue;
        }
        if (c == 'E' || c == 'e') {
            set_level(0);
            continue;
        }
        if (c == 'M' || c == 'm') {
            set_level(1);
            continue;
        }
        if (c == 'H' || c == 'h') {
            set_level(2);
            continue;
        }
        if (c == '?' || c == 'T' || c == 't') {
            show_hint();
            continue;
        }
        if (c == 'U' || c == 'u') {
            restore_undo();
            continue;
        }
        if (k == KNON)
            continue;
        if (k == KQUI) {
            quit = 1;
            return 0;
        }
        if (k == KLT) {
            step_select(-1);
        } else if (k == KRT) {
            step_select(1);
        } else if (k == KPLC) {
            if (pit[selected] == 0) {
                note("THAT PIT IS EMPTY");
            } else {
                save_undo();
                res = play_move(selected, 0);
                if (res == 2 && !game_over)
                    note("YOU LANDED IN YOUR STORE: GO AGAIN");
                else
                    return 0;
            }
        }
    }
    return 0;
}

/* computer_pick() - Pick the computer's pit. */
static int computer_pick(void)
{
    int i;
    int j;
    int n;
    int best;
    int bval;
    int val;
    int dep;
    int ord;
    int alp;
    int res;
    int mv[6];
    int nb[TOT];

    if (level == 0 && (random16() % 4) == 0)
        return random_pick();

    if (level == 0)
        dep = 2;
    else if (level == 1)
        dep = 3;
    else
        dep = 5;
    ord = (level >= 2) ? 1 : 0;

    best = 7;
    bval = -30000;
    alp = -30000;
    if (ord) {
        n = order_moves(pit, 1, mv);
    } else {
        n = 0;
        for (i = 7; i < CST; i++)
            if (pit[i] > 0)
                mv[n++] = i;
    }
    for (j = 0; j < n; j++) {
        i = mv[j];
        board_copy(nb, pit);
        res = board_sow(nb, i, 1);
        if (res == 2)
            val = alpha_beta(nb, 1, dep - 1, alp, 30000, ord);
        else
            val = alpha_beta(nb, 0, dep - 1, alp, 30000, ord);
        val += move_order_score(pit, i, 1) / 10;
        if (val > bval) {
            bval = val;
            best = i;
        }
        if (val > alp)
            alp = val;
    }
    return best;
}

/* hint_pick() - Pick a hard hint for the human. */
static int hint_pick(void)
{
    int i;
    int best;
    int bval;
    int val;
    int res;
    int nb[TOT];

    best = -1;
    bval = 30000;
    for (i = 0; i < HST; i++) {
        if (pit[i] == 0)
            continue;
        board_copy(nb, pit);
        res = board_sow(nb, i, 0);
        if (res == 2)
            val = alpha_beta(nb, 0, 4, -30000, bval, 1);
        else
            val = alpha_beta(nb, 1, 4, -30000, bval, 1);
        val -= move_order_score(pit, i, 0) / 10;
        if (val < bval) {
            bval = val;
            best = i;
        }
    }
    return best;
}

/* show_hint() - Show a human hint. */
static int show_hint(void)
{
    int i;

    note("THINKING ABOUT A HINT...");
    i = hint_pick();
    if (i < 0) {
        note("NO HINT: YOU HAVE NO LEGAL MOVE");
    } else {
        set_select(i);
        note("HINT: TRY THE HIGHLIGHTED PIT");
    }
    return 0;
}

/* think_pause() - Small thinking pause. */
static int think_pause(void)
{
    int i;
    int j;

    flush();
    for (i = 0; i < 250; i++)
        for (j = 0; j < 120; j++)
            ;
    return 0;
}

/* computer_turn() - Run the computer turns. */
static int computer_turn(void)
{
    int i;
    int res;

    while (!quit && !game_over) {
        note("COMPUTER IS THINKING...");
        think_pause();
        i = computer_pick();
        note("COMPUTER SOWS");
        res = play_move(i, 1);
        if (res == 2 && !game_over) {
            note("COMPUTER LANDED IN ITS STORE: AGAIN");
            think_pause();
        } else {
            return 0;
        }
    }
    return 0;
}

/* show_result() - Show the winner. */
static int show_result(void)
{
    clear_note_row(26);
    clear_note_row(27);
    clear_note_row(28);
    set_color(37);
    cursor_move(26, 5);
    if (quit)
        put_str("GAME QUIT");
    else if (pit[HST] > pit[CST])
        put_str("YOU WIN! MORE BEANS IN YOUR STORE");
    else if (pit[HST] < pit[CST])
        put_str("COMPUTER WINS THIS HARVEST");
    else
        put_str("DRAW GAME");
    cursor_move(27, 5);
    put_str("FINAL  YOU:");
    put_num2col(pit[HST]);
    put_str("  CPU:");
    put_num2col(pit[CST]);
    reset_color();
    flush();
    return 0;
}

/* ask_new_game() - Ask whether to start another game. */
static int ask_new_game(void)
{
    int c;

    if (quit)
        return 0;
    clear_note_row(28);
    set_color(37);
    cursor_move(28, 5);
    put_str("NEW GAME?  Y/N");
    reset_color();
    while (1) {
        c = key_read();
        if (c == 'Y' || c == 'y')
            return 1;
        if (c == 'N' || c == 'n' || c == KEYQ || c == KEYq ||
            c == ESC || c == XK_CC)
            return 0;
    }
}

/* main() - Game entry point. */
int main(void)
{
    int old_level;

    /* Install the single 8 KB console buffer once at startup. */
    setvbuf(stdout, console_buffer, _IOFBF, CBUF);

    init();
    while (!quit) {
        draw_all();
        while (!quit && !game_over) {
            if (has_legal_move(0))
                human();
            if (!quit && !game_over && has_legal_move(1))
                computer_turn();
            if (!has_legal_move(0) || !has_legal_move(1)) {
                collect_endgame();
                draw_positions();
            }
        }
        show_result();
        if (!ask_new_game())
            break;
        old_level = level;
        init();
        level = old_level;
    }

    cursor_move(SCRH, 1);
    show_cursor();
    reset_color();
    put_str("\r\n");
    flush();
    return 0;
}
