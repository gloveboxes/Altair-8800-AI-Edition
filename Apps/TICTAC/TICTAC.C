/*
 * 5x5 Tic-Tac-Toe for Altair 8800 - VT100/xterm.js compatible
 * dcc C on CP/M 2.2, using the shared service client.
 *
 * 5x5 board, get 4 in a row to win
 * Player (X) vs Computer (O) with heuristic AI
 * Tetris-style colorful VT100 cabinet display
 *
 * Controls:
 *   Arrow keys: Move cursor
 *   SPACE:      Place piece
 *   Q/ESC:      Quit
 */

#include <stdio.h>
#include <stdlib.h>

#include "../COMMON/DXSVC.H"

#define BSIZ 5
#define WLEN 4
#define BROW 5
#define BCOL 18
#define CWID 6
#define CHGT 3
#define NROW 8
#define NCOL 64

#define EMP 0
#define HUM 1
#define COMP 2

#define PLAY 0
#define DONE 1
#define QUIT 2

#define K_NON 0
#define K_UP 1
#define K_DN 2
#define K_LT 3
#define K_RT 4
#define K_PLC 5
#define K_QUI 6

int brd[BSIZ][BSIZ];
int crow, ccol;
int gstat;
int turn;
int hwin, cwin, drws;
int result;
int escst, escct;
static char console_buffer[2048];

static void cursor(int row, int column)
{
    printf("\033[%d;%dH", row, column);
}

static void color(int code)
{
    printf("\033[%dm", code);
}

static void reset_color(void)
{
    fputs("\033[0m", stdout);
}

static int read_key(void)
{
    return bdos(6, 255) & 255;
}

static void delay_ms(int timer, unsigned milliseconds)
{
    svc_start_timer(timer, milliseconds);
    while (svc_timer_active(timer))
        ;
}

/* pstr(s) - Print a NUL-terminated string. */
int pstr(s)
char *s;
{
    while (*s)
    {
        putchar(*s);
        s++;
    }
    return 0;
}

/* pbg(p) - Block background color for a piece. */
int pbg(p)
int p;
{
    if (p == HUM) return 41;
    if (p == COMP) return 44;
    return 100;
}

/* cbg(r,c) - Checker color for cabinet border. */
int cbg(r, c)
int r;
int c;
{
    if (((r / 2) + (c / 2)) & 1)
        return 100;
    return 107;
}

/* drwcel(r,c,hi) - Draw one board cell. hi=highlight. */
int drwcel(r, c, hi)
int r;
int c;
int hi;
{
    int sr, sc, p, bg, row;

    p = brd[r][c];
    sr = BROW + 1 + r * (CHGT + 1);
    sc = BCOL + 2 + c * (CWID + 1);

    if (hi && p == EMP)
        bg = 43;
    else
        bg = pbg(p);

    for (row = 0; row < CHGT; row++)
    {
        cursor(sr + row, sc);
        color(bg);

        if (hi && p != EMP && row == 0)
        {
            color(97);
            pstr("[    ]");
        }
        else if (hi && p != EMP && row == 2)
        {
            color(97);
            pstr("[    ]");
        }
        else if (row == 1 && p == HUM)
        {
            if (hi)
            {
                color(97);
                pstr("[");
                pstr(" XX ");
                pstr("]");
            }
            else
            {
                color(97);
                pstr("  XX  ");
            }
        }
        else if (row == 1 && p == COMP)
        {
            if (hi)
            {
                color(97);
                pstr("[");
                pstr(" OO ");
                pstr("]");
            }
            else
            {
                color(97);
                pstr("  OO  ");
            }
        }
        else if (row == 1 && hi)
        {
            color(30);
            pstr("  ..  ");
        }
        else
        {
            pstr("      ");
        }
        reset_color();
    }
    return 0;
}

/* drwgrd() - Draw grid separator lines. */
int drwgrd()
{
    int r, c, gr, gc;

    /* Horizontal grid lines */
    for (r = 1; r < BSIZ; r++)
    {
        gr = BROW + 1 + r * (CHGT + 1) - 1;
        cursor(gr, BCOL + 2);
        color(90);
        for (c = 0; c < BSIZ * (CWID + 1) - 1; c++)
            putchar('-');
        reset_color();
    }

    /* Vertical grid lines */
    for (c = 1; c < BSIZ; c++)
    {
        gc = BCOL + 2 + c * (CWID + 1) - 1;
        for (r = 0; r < BSIZ; r++)
        {
            gr = BROW + 1 + r * (CHGT + 1);
            cursor(gr, gc);
            color(90);
            putchar('|');
            cursor(gr + 1, gc);
            putchar('|');
            cursor(gr + 2, gc);
            putchar('|');
            reset_color();
        }
    }

    /* Cross points */
    for (r = 1; r < BSIZ; r++)
    {
        gr = BROW + 1 + r * (CHGT + 1) - 1;
        for (c = 1; c < BSIZ; c++)
        {
            gc = BCOL + 2 + c * (CWID + 1) - 1;
            cursor(gr, gc);
            color(90);
            putchar('+');
            reset_color();
        }
    }
    return 0;
}

/* drwbrd() - Draw the entire board. */
int drwbrd()
{
    int r, c;

    drwgrd();
    for (r = 0; r < BSIZ; r++)
        for (c = 0; c < BSIZ; c++)
            drwcel(r, c, 0);
    return 0;
}

/* border() - Draw the checker cabinet border. */
int border()
{
    int r, c, bw, bh;

    bw = BSIZ * (CWID + 1) + 3;
    bh = BSIZ * (CHGT + 1) + 1;
    for (r = 0; r < bh; r++)
    {
        cursor(BROW + r, BCOL);
        color(cbg(r, 0));
        putchar(' ');
        putchar(' ');
        reset_color();

        cursor(BROW + r, BCOL + bw - 2);
        color(cbg(r, bw - 1));
        putchar(' ');
        putchar(' ');
        reset_color();
    }

    /* Top and bottom rows */
    for (c = 0; c < bw; c++)
    {
        cursor(BROW, BCOL + c);
        color(cbg(0, c));
        putchar(' ');
        reset_color();

        cursor(BROW + bh - 1, BCOL + c);
        color(cbg(bh - 1, c));
        putchar(' ');
        reset_color();
    }
    reset_color();
    return 0;
}

/* title() - Draw heading and controls. */
int title()
{
    cursor(1, 1);
    color(36);
    pstr("TICTAC");
    reset_color();
    pstr(" for ");
    color(33);
    pstr("Altair 8800");
    reset_color();
    pstr(" V1.0");

    cursor(2, 1);
    color(37);
    pstr("Arrows Move  SPACE Place  Q/ESC Quit");
    reset_color();

    cursor(3, 1);
    color(90);
    pstr("5x5 board - get 4 in a row to win!");
    reset_color();
    return 0;
}

/* stats() - Draw score area. */
int stats()
{
    cursor(6, 1);
    color(35);
    pstr("SCORE");
    reset_color();

    cursor(8, 1);
    color(36);
    pstr("You(X): ");
    color(33);
    printf("%d", hwin);
    reset_color();
    pstr("   ");

    cursor(10, 1);
    color(36);
    pstr("CPU(O): ");
    color(33);
    printf("%d", cwin);
    reset_color();
    pstr("   ");

    cursor(12, 1);
    color(36);
    pstr("Draws:  ");
    color(33);
    printf("%d", drws);
    reset_color();
    pstr("   ");
    return 0;
}

/* turndr() - Draw whose turn it is. */
int turndr()
{
    cursor(NROW, NCOL);
    color(36);
    pstr("TURN");
    reset_color();

    cursor(NROW + 2, NCOL);
    if (turn == HUM)
    {
        color(41);
        color(97);
        pstr("  XX  ");
    }
    else
    {
        color(44);
        color(97);
        pstr("  OO  ");
    }
    reset_color();

    cursor(NROW + 4, NCOL);
    if (turn == HUM)
    {
        color(31);
        pstr("Your turn");
        pstr("      ");
    }
    else
    {
        color(34);
        pstr("CPU thinking");
        pstr("   ");
    }
    reset_color();
    return 0;
}

/* evline(r,c,dr,dc) - Evaluate line of WLEN for AI. */
int evline(r, c, dr, dc)
int r;
int c;
int dr;
int dc;
{
    int i, pr, pc, h, m;

    h = 0;
    m = 0;
    for (i = 0; i < WLEN; i++)
    {
        pr = r + i * dr;
        pc = c + i * dc;
        if (pr < 0 || pr >= BSIZ)
            return 0;
        if (pc < 0 || pc >= BSIZ)
            return 0;
        if (brd[pr][pc] == HUM) h++;
        if (brd[pr][pc] == COMP) m++;
    }
    if (h > 0 && m > 0)
        return 0;
    if (m == 3) return 10000;
    if (h == 3) return 5000;
    if (m == 2) return 100;
    if (h == 2) return 50;
    if (m == 1) return 10;
    if (h == 1) return 5;
    return 1;
}

/* aiscor(r,c) - Score an empty cell for AI. */
long aiscor(r, c)
int r;
int c;
{
    long sc;
    int off, s;

    sc = 0;

    /* Horizontal lines through (r,c) */
    for (off = 0; off < WLEN; off++)
    {
        s = evline(r, c - off, 0, 1);
        sc = sc + s;
    }

    /* Vertical lines */
    for (off = 0; off < WLEN; off++)
    {
        s = evline(r - off, c, 1, 0);
        sc = sc + s;
    }

    /* Diagonal \ lines */
    for (off = 0; off < WLEN; off++)
    {
        s = evline(r - off, c - off, 1, 1);
        sc = sc + s;
    }

    /* Diagonal / lines */
    for (off = 0; off < WLEN; off++)
    {
        s = evline(r - off, c + off, 1, -1);
        sc = sc + s;
    }

    /* Center and inner ring bonus */
    if (r == 2 && c == 2) sc = sc + 15;
    if (r >= 1 && r <= 3 && c >= 1 && c <= 3)
        sc = sc + 5;

    return sc;
}

/* aimove() - Computer selects and places a move. */
int aimove()
{
    int r, c, br, bc;
    long sc, best;
    int ties, rn;

    best = -1;
    br = 0;
    bc = 0;
    ties = 0;

    for (r = 0; r < BSIZ; r++)
    {
        for (c = 0; c < BSIZ; c++)
        {
            if (brd[r][c] != EMP)
                continue;
            sc = aiscor(r, c);
            if (sc > best)
            {
                best = sc;
                br = r;
                bc = c;
                ties = 1;
            }
            else if (sc == best)
            {
                ties++;
                rn = svc_random_u16() % ties;
                if (rn == 0)
                {
                    br = r;
                    bc = c;
                }
            }
        }
    }

    brd[br][bc] = COMP;
    drwcel(br, bc, 0);
    return 0;
}

/* chkwin(who) - Check if who has 4 in a row. */
int chkwin(who)
int who;
{
    int r, c, i, ok;

    /* Horizontal */
    for (r = 0; r < BSIZ; r++)
    {
        for (c = 0; c <= BSIZ - WLEN; c++)
        {
            ok = 1;
            for (i = 0; i < WLEN; i++)
            {
                if (brd[r][c + i] != who)
                    ok = 0;
            }
            if (ok) return 1;
        }
    }

    /* Vertical */
    for (c = 0; c < BSIZ; c++)
    {
        for (r = 0; r <= BSIZ - WLEN; r++)
        {
            ok = 1;
            for (i = 0; i < WLEN; i++)
            {
                if (brd[r + i][c] != who)
                    ok = 0;
            }
            if (ok) return 1;
        }
    }

    /* Diagonal \ */
    for (r = 0; r <= BSIZ - WLEN; r++)
    {
        for (c = 0; c <= BSIZ - WLEN; c++)
        {
            ok = 1;
            for (i = 0; i < WLEN; i++)
            {
                if (brd[r + i][c + i] != who)
                    ok = 0;
            }
            if (ok) return 1;
        }
    }

    /* Diagonal / */
    for (r = 0; r <= BSIZ - WLEN; r++)
    {
        for (c = WLEN - 1; c < BSIZ; c++)
        {
            ok = 1;
            for (i = 0; i < WLEN; i++)
            {
                if (brd[r + i][c - i] != who)
                    ok = 0;
            }
            if (ok) return 1;
        }
    }

    return 0;
}

/* chkful() - Check if board is full (draw). */
int chkful()
{
    int r, c;

    for (r = 0; r < BSIZ; r++)
        for (c = 0; c < BSIZ; c++)
            if (brd[r][c] == EMP)
                return 0;
    return 1;
}

/* keymap(ch) - Decode key with ANSI sequence support. */
int keymap(ch)
int ch;
{
    if (escst == 2)
    {
        escst = 0;
        if (ch == 'A') return K_UP;
        if (ch == 'B') return K_DN;
        if (ch == 'C') return K_RT;
        if (ch == 'D') return K_LT;
        return K_NON;
    }

    if (escst == 1)
    {
        escst = 0;
        if (ch == '[')
        {
            escst = 2;
            return K_NON;
        }
        return K_QUI;
    }

    if (ch == 27)
    {
        escst = 1;
        escct = 0;
        return K_NON;
    }

    if (ch == 'q' || ch == 'Q') return K_QUI;
    if (ch == 5) return K_UP;
    if (ch == 24) return K_DN;
    if (ch == 19) return K_LT;
    if (ch == 4) return K_RT;
    if (ch == ' ') return K_PLC;
    return K_NON;
}

/* esctik() - Lone ESC becomes quit after grace period. */
int esctik()
{
    if (escst == 1)
    {
        escct++;
        if (escct > 2)
        {
            escst = 0;
            gstat = QUIT;
        }
    }
    return 0;
}

/* dokey(k) - Handle one decoded key action. */
int dokey(k)
int k;
{
    if (k == K_QUI)
    {
        gstat = QUIT;
        return 0;
    }

    if (k == K_UP && crow > 0)
    {
        drwcel(crow, ccol, 0);
        crow--;
        drwcel(crow, ccol, 1);
    }
    else if (k == K_DN && crow < BSIZ - 1)
    {
        drwcel(crow, ccol, 0);
        crow++;
        drwcel(crow, ccol, 1);
    }
    else if (k == K_LT && ccol > 0)
    {
        drwcel(crow, ccol, 0);
        ccol--;
        drwcel(crow, ccol, 1);
    }
    else if (k == K_RT && ccol < BSIZ - 1)
    {
        drwcel(crow, ccol, 0);
        ccol++;
        drwcel(crow, ccol, 1);
    }
    else if (k == K_PLC && brd[crow][ccol] == EMP)
    {
        brd[crow][ccol] = HUM;
        drwcel(crow, ccol, 0);
        if (chkwin(HUM))
        {
            result = 1;
            gstat = DONE;
        }
        else if (chkful())
        {
            result = 3;
            gstat = DONE;
        }
        else
        {
            turn = COMP;
            turndr();
        }
    }
    return 0;
}

/* hmturn() - Run one tick of the human turn. */
int hmturn()
{
    int ch, k;

    svc_start_timer(2, 25);
    while (gstat == PLAY && turn == HUM && svc_timer_active(2))
    {
        ch = read_key();
        if (ch == 0) continue;
        k = keymap(ch);
        if (k != K_NON)
            dokey(k);
        fflush(stdout);
    }
    esctik();
    return 0;
}

/* cpturn() - Run the CPU turn. */
int cpturn()
{
    delay_ms(1, 400);
    aimove();

    if (chkwin(COMP))
    {
        result = 2;
        gstat = DONE;
    }
    else if (chkful())
    {
        result = 3;
        gstat = DONE;
    }
    else
    {
        turn = HUM;
        turndr();
        drwcel(crow, ccol, 1);
    }
    return 0;
}

/* overdr() - Draw game result message. */
int overdr()
{
    cursor(16, 1);
    if (result == 1)
    {
        color(32);
        pstr("YOU WIN!    ");
        hwin++;
    }
    else if (result == 2)
    {
        color(31);
        pstr("CPU WINS!   ");
        cwin++;
    }
    else
    {
        color(33);
        pstr("DRAW!       ");
        drws++;
    }
    reset_color();

    stats();

    cursor(18, 1);
    color(37);
    pstr("Play again? (Y/N)");
    reset_color();
    return 0;
}

/* replay() - Ask play again, return 1 to continue. */
int replay()
{
    int ch;

    overdr();
    fflush(stdout);
    while (1)
    {
        ch = read_key();
        if (ch == 'y' || ch == 'Y')
            return 1;
        if (ch == 'n' || ch == 'N')
            return 0;
        if (ch == 27)
            return 0;
    }
}

/* ginit() - Reset board and state for new game. */
int ginit()
{
    int r, c;

    for (r = 0; r < BSIZ; r++)
        for (c = 0; c < BSIZ; c++)
            brd[r][c] = EMP;

    crow = 2;
    ccol = 2;
    gstat = PLAY;
    turn = HUM;
    result = 0;
    escst = 0;
    escct = 0;
    return 0;
}

/* main() - Program entry point. */
int main()
{
    int run;

    setvbuf(stdout, console_buffer, _IOFBF, sizeof(console_buffer));
    hwin = 0;
    cwin = 0;
    drws = 0;
    run = 1;

    while (run)
    {
        ginit();
        fputs("\033[2J\033[H\033[?25l", stdout);
        title();
        border();
        drwbrd();
        stats();
        turndr();
        drwcel(crow, ccol, 1);
        fflush(stdout);

        while (gstat == PLAY)
        {
            if (turn == HUM)
                hmturn();
            else
                cpturn();
            fflush(stdout);
        }

        if (gstat == QUIT)
            break;

        run = replay();
    }

    cursor(28, 1);
    fputs("\033[?25h", stdout);
    reset_color();
    pstr("Thanks for playing!\r\n");
    fflush(stdout);
    return 0;
}
