/*
 * 5x5 Tic-Tac-Toe for Altair 8800 - VT100/xterm.js compatible
 * dcc C on CP/M 2.2, using the shared service client.
 *
 * 5x5 board, get 4 in a row to win
 * Draw early when every winning line is blocked
 * Player (X) vs Computer (O) with two-ply alpha-beta minimax
 * Random CPU opening in the central 3x3
 * Tetris-style colorful VT100 cabinet display
 *
 * Controls:
 *   Arrow keys: Move cursor
 *   SPACE:      Place piece
 *   T:          Tip (highlight a recommended move)
 *   Q/ESC:      Quit
 */

#include <stdlib.h>

#include "../../COMMON/DXSVC.H"

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
#define K_TIP 7
#define CONSOLE_BUFFER_SIZE 2048
#define AI_DEPTH 2
#define AI_WIN 30000
#define AI_DRAW 31000
#define AI_INF 32000
#define WIN_LINES 28

int brd[BSIZ][BSIZ];
int crow, ccol;
int gstat;
int turn;
int hwin, cwin, drws;
int result;
int escst, escct;
static unsigned char console_buffer[CONSOLE_BUFFER_SIZE];
static unsigned console_buffer_used;
static const unsigned char wincells[WIN_LINES][WLEN] = {
    {0, 1, 2, 3}, {1, 2, 3, 4},
    {5, 6, 7, 8}, {6, 7, 8, 9},
    {10, 11, 12, 13}, {11, 12, 13, 14},
    {15, 16, 17, 18}, {16, 17, 18, 19},
    {20, 21, 22, 23}, {21, 22, 23, 24},
    {0, 5, 10, 15}, {5, 10, 15, 20},
    {1, 6, 11, 16}, {6, 11, 16, 21},
    {2, 7, 12, 17}, {7, 12, 17, 22},
    {3, 8, 13, 18}, {8, 13, 18, 23},
    {4, 9, 14, 19}, {9, 14, 19, 24},
    {0, 6, 12, 18}, {1, 7, 13, 19},
    {5, 11, 17, 23}, {6, 12, 18, 24},
    {3, 7, 11, 15}, {4, 8, 12, 16},
    {8, 12, 16, 20}, {9, 13, 17, 21}
};
static const unsigned char central[9] = {6, 7, 8, 11, 12, 13, 16, 17, 18};
static unsigned char aiboard[BSIZ * BSIZ];
static unsigned char aiorder[BSIZ * BSIZ];
static int aicount;
static int tipactive;

static void console_flush(void)
{
    unsigned index;

    /* BIOS CONOUT bypasses BDOS look-ahead/flow control for every byte. */
    for (index = 0; index < console_buffer_used; ++index)
        bios(4, console_buffer[index]);
    console_buffer_used = 0;
}

static inline void console_put_byte(unsigned char byte)
{
    if (console_buffer_used == CONSOLE_BUFFER_SIZE)
        console_flush();
    console_buffer[console_buffer_used++] = byte;
}

static void write_text(const char *text)
{
    unsigned char byte;

    while (*text) {
        byte = (unsigned char)*text++;
        console_put_byte(byte);
    }
}

static void write_int(int value)
{
    char digits[5];
    unsigned magnitude;
    unsigned count = 0;
    unsigned char byte;

    if (value < 0)
        console_put_byte('-');
    /* Prime the conversion loop on both paths for dcc's loop optimizer. */
    magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    do {
        digits[count++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    while (count != 0) {
        byte = digits[--count];
        console_put_byte(byte);
    }
}

static void cursor(int row, int column)
{
    write_text("\033[");
    write_int(row);
    console_put_byte(';');
    write_int(column);
    console_put_byte('H');
}

static void color(int code)
{
    write_text("\033[");
    write_int(code);
    console_put_byte('m');
}

static void reset_color(void)
{
    write_text("\033[0m");
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
int pstr(const char *s)
{
    write_text(s);
    return 0;
}

/* pbg(p) - Block background color for a piece. */
int pbg(int p)
{
    if (p == HUM) return 41;
    if (p == COMP) return 44;
    return 100;
}

/* cbg(r,c) - Checker color for cabinet border. */
int cbg(int r, int c)
{
    if (((r / 2) + (c / 2)) & 1)
        return 100;
    return 107;
}

/* drwcel(r,c,hi) - Draw one board cell. hi=highlight. */
int drwcel(int r, int c, int hi)
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
int drwgrd(void)
{
    int r, c, gr, gc;

    /* Horizontal grid lines */
    for (r = 1; r < BSIZ; r++)
    {
        gr = BROW + 1 + r * (CHGT + 1) - 1;
        cursor(gr, BCOL + 2);
        color(90);
        for (c = 0; c < BSIZ * (CWID + 1) - 1; c++)
            console_put_byte('-');
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
            console_put_byte('|');
            cursor(gr + 1, gc);
            console_put_byte('|');
            cursor(gr + 2, gc);
            console_put_byte('|');
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
            console_put_byte('+');
            reset_color();
        }
    }
    return 0;
}

/* drwbrd() - Draw the entire board. */
int drwbrd(void)
{
    int r, c;

    drwgrd();
    for (r = 0; r < BSIZ; r++)
        for (c = 0; c < BSIZ; c++)
            drwcel(r, c, 0);
    return 0;
}

/* border() - Draw the checker cabinet border. */
int border(void)
{
    int r, c, bw, bh;

    bw = BSIZ * (CWID + 1) + 3;
    bh = BSIZ * (CHGT + 1) + 1;
    for (r = 0; r < bh; r++)
    {
        cursor(BROW + r, BCOL);
        color(cbg(r, 0));
        console_put_byte(' ');
        console_put_byte(' ');
        reset_color();

        cursor(BROW + r, BCOL + bw - 2);
        color(cbg(r, bw - 1));
        console_put_byte(' ');
        console_put_byte(' ');
        reset_color();
    }

    /* Top and bottom rows */
    for (c = 0; c < bw; c++)
    {
        cursor(BROW, BCOL + c);
        color(cbg(0, c));
        console_put_byte(' ');
        reset_color();

        cursor(BROW + bh - 1, BCOL + c);
        color(cbg(bh - 1, c));
        console_put_byte(' ');
        reset_color();
    }
    reset_color();
    return 0;
}

/* title() - Draw heading and controls. */
int title(void)
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
    pstr("Arrows Move  SPACE Place  T Tip  Q/ESC Quit");
    reset_color();

    cursor(3, 1);
    color(90);
    pstr("5x5 board - get 4 in a row to win!");
    reset_color();
    return 0;
}

/* stats() - Draw score area. */
int stats(void)
{
    cursor(6, 1);
    color(35);
    pstr("SCORE");
    reset_color();

    cursor(8, 1);
    color(36);
    pstr("You(X): ");
    color(33);
    write_int(hwin);
    reset_color();
    pstr("   ");

    cursor(10, 1);
    color(36);
    pstr("CPU(O): ");
    color(33);
    write_int(cwin);
    reset_color();
    pstr("   ");

    cursor(12, 1);
    color(36);
    pstr("Draws:  ");
    color(33);
    write_int(drws);
    reset_color();
    pstr("   ");
    return 0;
}

/* turndr() - Draw whose turn it is. */
int turndr(void)
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

static int aieval(void)
{
    static const int weights[WLEN] = {0, 1, 10, 100};
    int line, i, piece, h, m, live = 0, score = 0;

    for (line = 0; line < WIN_LINES; line++)
    {
        h = m = 0;
        for (i = 0; i < WLEN; i++)
        {
            piece = aiboard[wincells[line][i]];
            if (piece == HUM) h++;
            if (piece == COMP) m++;
        }
        if (h == WLEN) return -AI_WIN;
        if (m == WLEN) return AI_WIN;
        if (h && m) continue;
        live = 1;
        score += weights[m] - weights[h];
    }
    if (!live) return AI_DRAW;
    for (i = 0; i < 9; i++)
    {
        piece = aiboard[central[i]];
        h = i == 4 ? 20 : 5;
        if (piece == EMP) continue;
        score += piece == COMP ? h : -h;
    }
    return score;
}

static void aiprep(int who)
{
    int i, j, cell, score, scores[BSIZ * BSIZ];

    for (i = 0; i < BSIZ * BSIZ; i++)
        aiboard[i] = brd[i / BSIZ][i % BSIZ];
    aicount = 0;
    for (cell = 0; cell < BSIZ * BSIZ; cell++)
    {
        if (aiboard[cell] != EMP) continue;
        aiboard[cell] = who;
        score = aieval();
        aiboard[cell] = EMP;
        if (score == AI_DRAW) score = 0;
        j = aicount;
        while (j > 0 && (who == COMP ? scores[j - 1] < score :
                                       scores[j - 1] > score))
        {
            scores[j] = scores[j - 1];
            aiorder[j] = aiorder[j - 1];
            j--;
        }
        scores[j] = score;
        aiorder[j] = cell;
        aicount++;
    }
}

static int minimax(int depth, int who, int alpha, int beta)
{
    int i, cell, score, best;

    score = aieval();
    if (score == AI_WIN) return AI_WIN + depth;
    if (score == -AI_WIN) return -AI_WIN - depth;
    if (score == AI_DRAW) return 0;
    if (depth == 0) return score;

    best = who == COMP ? -AI_INF : AI_INF;
    for (i = 0; i < aicount; i++)
    {
        cell = aiorder[i];
        if (aiboard[cell] != EMP) continue;
        aiboard[cell] = who;
        score = minimax(depth - 1, HUM + COMP - who, alpha, beta);
        aiboard[cell] = EMP;
        if (who == COMP)
        {
            if (score > best) best = score;
            if (best > alpha) alpha = best;
        }
        else
        {
            if (score < best) best = score;
            if (best < beta) beta = best;
        }
        if (alpha >= beta) break;
    }
    return best;
}

static int aiopen(void)
{
    int r, c, i, cell, humans = 0, count = 0;
    int cells[9];

    /* Only the first reply: exactly one X and no O on the board. */
    for (r = 0; r < BSIZ; r++)
        for (c = 0; c < BSIZ; c++)
        {
            if (brd[r][c] == COMP)
                return -1;
            if (brd[r][c] == HUM)
                humans++;
        }
    if (humans != 1)
        return -1;

    for (i = 0; i < 9; i++)
    {
        cell = central[i];
        if (brd[cell / BSIZ][cell % BSIZ] == EMP)
            cells[count++] = cell;
    }

    return cells[svc_random_u16() % count];
}

static int aichoose(int who)
{
    int i, cell, score, best, ties = 0;
    int choices[BSIZ * BSIZ];

    aiprep(who);
    if (aicount == 0) return -1;
    best = who == COMP ? -AI_INF : AI_INF;
    for (i = 0; i < aicount; i++)
    {
        cell = aiorder[i];
        aiboard[cell] = who;
        if (who == COMP)
            score = minimax(AI_DEPTH - 1, HUM, best, AI_INF);
        else
            score = minimax(AI_DEPTH - 1, COMP, -AI_INF, best);
        /* A cutoff equal to best is only a bound, not an exact tie. */
        if (score == best)
            score = minimax(AI_DEPTH - 1, HUM + COMP - who, -AI_INF, AI_INF);
        aiboard[cell] = EMP;
        if (who == COMP ? score > best : score < best)
        {
            best = score;
            ties = 0;
        }
        if (score == best)
            choices[ties++] = cell;
    }
    return choices[ties > 1 ? svc_random_u16() % ties : 0];
}

/* aimove() - Computer selects and places a move. */
int aimove(void)
{
    int cell;

    cell = aiopen();
    if (cell < 0) cell = aichoose(COMP);
    if (cell < 0)
    {
        result = 3;
        gstat = DONE;
        return 0;
    }

    brd[cell / BSIZ][cell % BSIZ] = COMP;
    drwcel(cell / BSIZ, cell % BSIZ, 0);
    return 0;
}

/* chkwin(who) - Check if who has 4 in a row. */
int chkwin(int who)
{
    int line, i, cell, ok;

    for (line = 0; line < WIN_LINES; line++)
    {
        ok = 1;
        for (i = 0; i < WLEN; i++)
        {
            cell = wincells[line][i];
            if (brd[cell / BSIZ][cell % BSIZ] != who)
                ok = 0;
        }
        if (ok) return 1;
    }
    return 0;
}

/* chkful() - Check if board is full (draw). */
int chkful(void)
{
    int r, c;

    for (r = 0; r < BSIZ; r++)
        for (c = 0; c < BSIZ; c++)
            if (brd[r][c] == EMP)
                return 0;
    return 1;
}

static int winleft(void)
{
    int line, i, cell, pieces;

    for (line = 0; line < WIN_LINES; line++)
    {
        pieces = 0;
        for (i = 0; i < WLEN; i++)
        {
            cell = wincells[line][i];
            pieces |= brd[cell / BSIZ][cell % BSIZ];
        }
        if (pieces != (HUM | COMP)) return 1;
    }
    return 0;
}

static void tiptext(const char *text)
{
    cursor(26, BCOL);
    write_text("\033[K");
    color(36);
    pstr(text);
    reset_color();
}

static void give_tip(void)
{
    int cell;

    if (gstat != PLAY || turn != HUM)
    {
        tiptext("Tips are only available on your turn.");
        console_flush();
        return;
    }
    tiptext("Finding tip...");
    console_flush();
    cell = aichoose(HUM);
    if (cell < 0)
    {
        tiptext("No legal moves available.");
        console_flush();
        return;
    }
    drwcel(crow, ccol, 0);
    crow = cell / BSIZ;
    ccol = cell % BSIZ;
    drwcel(crow, ccol, 1);
    tipactive = 1;
    tiptext("Tip: highlighted square. SPACE to play.");
    console_flush();
}

/* keymap(ch) - Decode key with ANSI sequence support. */
int keymap(int ch)
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
    if (ch == 't' || ch == 'T') return K_TIP;
    if (ch == 5) return K_UP;
    if (ch == 24) return K_DN;
    if (ch == 19) return K_LT;
    if (ch == 4) return K_RT;
    if (ch == ' ') return K_PLC;
    return K_NON;
}

/* esctik() - Lone ESC becomes quit after grace period. */
int esctik(void)
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
int dokey(int k)
{
    if (k == K_QUI)
    {
        gstat = QUIT;
        return 0;
    }
    if (k == K_TIP)
    {
        give_tip();
        return 0;
    }
    if (tipactive && k >= K_UP && k <= K_PLC)
    {
        tiptext("");
        tipactive = 0;
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
        else if (!winleft())
        {
            result = 4;
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
int hmturn(void)
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
        console_flush();
    }
    esctik();
    return 0;
}

/* cpturn() - Run the CPU turn. */
int cpturn(void)
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
    else if (!winleft())
    {
        result = 4;
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
int overdr(void)
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
        if (result == 4)
        {
            cursor(26, BCOL);
            color(5);
            pstr("No winning moves left.");
        }
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
int replay(void)
{
    int ch;

    overdr();
    console_flush();
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
int ginit(void)
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
    tipactive = 0;
    return 0;
}

/* main() - Program entry point. */
int main(void)
{
    int run;

    hwin = 0;
    cwin = 0;
    drws = 0;
    run = 1;

    while (run)
    {
        ginit();
        write_text("\033[2J\033[H\033[?25l");
        title();
        border();
        drwbrd();
        stats();
        turndr();
        drwcel(crow, ccol, 1);
        console_flush();

        while (gstat == PLAY)
        {
            if (turn == HUM)
                hmturn();
            else
                cpturn();
            console_flush();
        }

        if (gstat == QUIT)
            break;

        run = replay();
    }

    cursor(28, 1);
    write_text("\033[?25h");
    reset_color();
    pstr("Thanks for playing!\r\n");
    console_flush();
    return 0;
}
