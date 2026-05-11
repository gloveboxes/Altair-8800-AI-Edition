/*
 * MANCALA.C - Mancala / African Bean Game for Altair 8800 CP/M.
 *
 * BDS C 1.6, VT100/xterm.js display, 30 rows by 80 columns.
 * Player vs computer, Kalah-style rules.
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

#include "dxterm.h"
#include "dxsys.h"

#define PITS 6
#define TOT 14
#define HST 6
#define CST 13

#define SCRW 80
#define SCRH 30
#define MINC 3
#define MAXC 78

#define BROW 7
#define BCOL 18
#define PCW 7
#define PCH 4
#define PNLT 5
#define PNLL 5
#define PNLW 72
#define PNLH 21

#define KEYQ 81
#define KEYq 113
#define KEYR 13

#define KNON 0
#define KLT 1
#define KRT 2
#define KPLC 3
#define KQUI 4

int pit[TOT];
int upit[TOT];
int sel;
int usel;
int quit;
int over;
int escst;
int lvl;
int uok;

int hshow();

/* pstr(s) - Print a NUL-terminated string. */
int pstr(s)
char *s;
{
    while (*s)
    {
        x_cout(*s);
        s++;
    }
    return 0;
}

/* num2(n) - Print a two-column number. */
int num2(n)
int n;
{
    if (n < 10)
        x_cout(' ');
    x_numpr(n);
    return 0;
}

/* wood() - Select the board's dark wood color. */
int wood()
{
    x_setc(44);
    x_setc(97);
    return 0;
}

/* hole() - Select recessed pit color. */
int hole()
{
    x_setc(41);
    x_setc(97);
    return 0;
}

/* cbg(r,c) - African-inspired checker border color. */
int cbg(r, c)
int r;
int c;
{
    if (((r / 2) + (c / 2)) & 1)
    {
        if (r & 1)
            return 42;
        return 43;
    }
    if (c & 1)
        return 41;
    return 44;
}

/* brdr() - Draw the checker outline. */
int brdr()
{
    int r;
    int c;

    for (c = 0; c < SCRW; c += 2)
    {
        x_setc(cbg(0, c / 2));
        x_curmv(2, c + 1);
        x_cout(' ');
        x_cout(' ');
        x_setc(cbg(SCRH - 1, c / 2));
        x_curmv(SCRH, c + 1);
        x_cout(' ');
        x_cout(' ');
    }

    for (r = 3; r < SCRH; r++)
    {
        x_setc(cbg(r - 2, 0));
        x_curmv(r, 1);
        x_cout(' ');
        x_cout(' ');
        x_setc(cbg(r - 2, (SCRW / 2) - 1));
        x_curmv(r, SCRW - 1);
        x_cout(' ');
        x_cout(' ');
    }
    x_rstc();
    return 0;
}

/* stat() - Draw score stores and turn. */
int stat()
{
    x_rstc();
    x_setc(37);
    x_curmv(1, 3);
    pstr("MANCALA - THE AFRICAN BEAN GAME");
    x_curmv(1, 49);
    pstr("YOU:");
    num2(pit[HST]);
    pstr("  CPU:");
    num2(pit[CST]);
    pstr("  ");
    if (lvl == 0)
        pstr("E:EASY");
    else if (lvl == 1)
        pstr("M:MED");
    else
        pstr("H:HARD");
    x_ereol();
    return 0;
}

/* pcol(i) - Screen column for pit i. */
int pcol(i)
int i;
{
    int c;

    if (i < HST)
        c = BCOL + (i * (PCW + 1));
    else
        c = BCOL + ((12 - i) * (PCW + 1));
    return c;
}

/* prow(i) - Screen row for pit i. */
int prow(i)
int i;
{
    if (i < HST)
        return BROW + 10;
    return BROW + 1;
}

/* bean(n) - Draw bean dots for a pit. */
int bean(n)
int n;
{
    int i;
    int m;

    m = n;
    if (m > 5)
        m = 5;
    for (i = 0; i < m; i++)
        x_cout('o');
    for (; i < 6; i++)
        x_cout(' ');
    return 0;
}

/* drpit(i,hi) - Draw one small pit. */
int drpit(i, hi)
int i;
int hi;
{
    int r;
    int c;
    int row;

    r = prow(i);
    c = pcol(i);
    for (row = 0; row < PCH; row++)
    {
        x_curmv(r + row, c);
        if (hi)
        {
            x_setc(43);
            x_setc(30);
        }
        else
            hole();
        if (row == 0 || row == PCH - 1)
            pstr("       ");
        else if (row == 1)
        {
            x_cout(' ');
            num2(pit[i]);
            pstr("    ");
        }
        else
        {
            x_cout(' ');
            if (pit[i] > 0)
                bean(pit[i]);
            else
                pstr("      ");
        }
        x_rstc();
    }
    return 0;
}

/* drstor(i) - Draw one store. */
int drstor(i)
int i;
{
    int r;
    int c;
    int row;

    if (i == HST)
        c = 67;
    else
        c = 8;
    r = BROW + 4;
    for (row = 0; row < 8; row++)
    {
        x_curmv(r + row, c);
        hole();
        if (row == 0 || row == 7)
            pstr("       ");
        else if (row == 2)
        {
            pstr("  ");
            num2(pit[i]);
            pstr("   ");
        }
        else if (row == 4)
        {
            pstr(" ");
            if (i == HST)
                pstr("YOU ");
            else
                pstr("CPU ");
            pstr("  ");
        }
        else
            pstr("       ");
        x_rstc();
    }
    return 0;
}

/* clrnot(r) - Clear a message row without touching border. */
int clrnot(r)
int r;
{
    int i;

    x_rstc();
    x_curmv(r, 5);
    for (i = 0; i < 72; i++)
        x_cout(' ');
    return 0;
}

/* panel() - Draw the carved wooden board panel. */
int panel()
{
    int r;
    int c;

    for (r = 0; r < PNLH; r++)
    {
        x_curmv(PNLT + r, PNLL);
        wood();
        for (c = 0; c < PNLW; c++)
            x_cout(' ');
        x_rstc();
    }
    for (r = PNLT + 3; r < PNLT + PNLH - 2; r += 4)
    {
        x_curmv(r, PNLL + 4);
        wood();
        x_setc(33);
        pstr("................................................................");
        x_rstc();
    }
    return 0;
}

/* grain() - Add carved board lines. */
int grain()
{
    int r;

    x_setc(33);
    for (r = 9; r <= 23; r += 4)
    {
        x_curmv(r, 13);
        pstr("......................................................");
    }
    x_rstc();
    return 0;
}

/* labels() - Draw pit labels. */
int labels()
{
    int i;

    x_rstc();
    wood();
    x_setc(93);
    x_curmv(6, 30);
    pstr("COMPUTER SIDE");
    x_rstc();
    wood();
    x_setc(93);
    x_curmv(24, 33);
    pstr("YOUR SIDE");
    for (i = 0; i < PITS; i++)
    {
        wood();
        x_setc(93);
        x_curmv(BROW + 15, pcol(i) + 3);
        x_cout('1' + i);
        x_rstc();
    }
    x_rstc();
    return 0;
}

/* note(s) - Draw status message. */
int note(s)
char *s;
{
    clrnot(27);
    x_setc(37);
    x_curmv(27, 5);
    pstr(s);
    x_rstc();
    return 0;
}

/* help() - Draw persistent shortcut keys. */
int help()
{
    clrnot(28);
    x_setc(37);
    x_curmv(28, 5);
    pstr("KEYS: 1-6 SELECT  SPACE SOW  U UNDO  E/M/H LEVEL  ? HINT  Q QUIT");
    x_rstc();
    return 0;
}

/* drall() - Draw board and pits. */
int drall()
{
    int i;

    x_clrsc();
    x_hidcr();
    brdr();
    stat();
    panel();
    labels();
    drstor(CST);
    drstor(HST);
    for (i = 0; i < HST; i++)
        drpit(i, i == sel);
    for (i = 7; i < CST; i++)
        drpit(i, 0);
    note("YOUR TURN: CHOOSE A PIT WITH BEANS");
    help();
    return 0;
}

/* drpos() - Redraw pit and store positions only. */
int drpos()
{
    int i;

    stat();
    drstor(CST);
    drstor(HST);
    for (i = 0; i < HST; i++)
        drpit(i, i == sel);
    for (i = 7; i < CST; i++)
        drpit(i, 0);
    return 0;
}

/* redraw(i) - Redraw pit or store by index. */
int redraw(i)
int i;
{
    if (i == HST || i == CST)
        drstor(i);
    else if (i < HST)
        drpit(i, i == sel);
    else
        drpit(i, 0);
    stat();
    return 0;
}

/* init() - Initialize game state. */
int init()
{
    int i;

    for (i = 0; i < TOT; i++)
        pit[i] = 4;
    pit[HST] = 0;
    pit[CST] = 0;
    sel = 0;
    quit = 0;
    over = 0;
    escst = 0;
    lvl = 1;
    uok = 0;
    return 0;
}

/* keymap(c) - Decode raw or translated control keys. */
int keymap(c)
int c;
{
    if (escst == 2)
    {
        if (c == 0)
            return KNON;
        escst = 0;
        if (c == 'C')
            return KRT;
        if (c == 'D')
            return KLT;
        return KNON;
    }

    if (escst == 1)
    {
        if (c == 0)
            return KNON;
        escst = 0;
        if (c == '[')
        {
            escst = 2;
            return KNON;
        }
        return KQUI;
    }

    if (c == 0)
        return KNON;
    if (c == XK_ESC)
    {
        escst = 1;
        return KNON;
    }
    if (x_iscc(c) || c == KEYQ || c == KEYq)
        return KQUI;
    if (x_islt(c))
        return KLT;
    if (x_isrt(c))
        return KRT;
    if (x_isspc(c) || c == KEYR)
        return KPLC;
    return KNON;
}

/* getact() - Read and decode one pending action. */
int getact()
{
    int c;

    c = x_keyrd();
    return keymap(c);
}

/* sumh() - Count beans on human side. */
int sumh()
{
    int i;
    int s;

    s = 0;
    for (i = 0; i < HST; i++)
        s += pit[i];
    return s;
}

/* sumc() - Count beans on computer side. */
int sumc()
{
    int i;
    int s;

    s = 0;
    for (i = 7; i < CST; i++)
        s += pit[i];
    return s;
}

/* colend() - Collect remaining beans at game end. */
int colend()
{
    int i;

    for (i = 0; i < HST; i++)
    {
        pit[HST] += pit[i];
        pit[i] = 0;
    }
    for (i = 7; i < CST; i++)
    {
        pit[CST] += pit[i];
        pit[i] = 0;
    }
    over = 1;
    return 0;
}

/* opp(i) - Return opposite pit index. */
int opp(i)
int i;
{
    return 12 - i;
}

/* owned(i,p) - Test whether pit i belongs to player p. */
int owned(i, p)
int i;
int p;
{
    if (p == 0 && i >= 0 && i < HST)
        return 1;
    if (p == 1 && i > HST && i < CST)
        return 1;
    return 0;
}

/* store(p) - Return store index for player p. */
int store(p)
int p;
{
    if (p == 0)
        return HST;
    return CST;
}

/* skip(i,p) - Test whether index i is opponent store. */
int skip(i, p)
int i;
int p;
{
    if (p == 0 && i == CST)
        return 1;
    if (p == 1 && i == HST)
        return 1;
    return 0;
}

/* move(i,p) - Sow from pit i for player p. */
int move(i, p)
int i;
int p;
{
    int cnt;
    int pos;
    int op;
    int st;

    if (!owned(i, p) || pit[i] == 0)
        return 0;
    cnt = pit[i];
    pit[i] = 0;
    redraw(i);
    pos = i;
    while (cnt > 0)
    {
        pos++;
        if (pos >= TOT)
            pos = 0;
        if (!skip(pos, p))
        {
            pit[pos]++;
            cnt--;
            redraw(pos);
        }
    }

    st = store(p);
    if (owned(pos, p) && pit[pos] == 1)
    {
        op = opp(pos);
        if (pit[op] > 0)
        {
            pit[st] += pit[op] + 1;
            pit[op] = 0;
            pit[pos] = 0;
            redraw(op);
            redraw(pos);
            redraw(st);
            if (p == 0)
                note("CAPTURE! THE OPPOSITE PIT FALLS TO YOU");
            else
                note("COMPUTER CAPTURES FROM THE OPPOSITE PIT");
        }
    }

    if (sumh() == 0 || sumc() == 0)
    {
        colend();
        drpos();
        return 0;
    }
    if (pos == st)
        return 2;
    return 1;
}

/* legal(p) - Return non-zero if player has a move. */
int legal(p)
int p;
{
    int i;

    if (p == 0)
    {
        for (i = 0; i < HST; i++)
            if (pit[i] > 0)
                return 1;
    }
    else
    {
        for (i = 7; i < CST; i++)
            if (pit[i] > 0)
                return 1;
    }
    return 0;
}

/* bcopy() - Copy a simulated board. */
int bcopy(dst, src)
int dst[];
int src[];
{
    int i;

    for (i = 0; i < TOT; i++)
        dst[i] = src[i];
    return 0;
}

/* bsumh() - Count simulated human beans. */
int bsumh(b)
int b[];
{
    int i;
    int s;

    s = 0;
    for (i = 0; i < HST; i++)
        s += b[i];
    return s;
}

/* bsumc() - Count simulated computer beans. */
int bsumc(b)
int b[];
{
    int i;
    int s;

    s = 0;
    for (i = 7; i < CST; i++)
        s += b[i];
    return s;
}

/* blegal() - Test simulated legal move. */
int blegal(b, p)
int b[];
int p;
{
    int i;

    if (p == 0)
    {
        for (i = 0; i < HST; i++)
            if (b[i] > 0)
                return 1;
    }
    else
    {
        for (i = 7; i < CST; i++)
            if (b[i] > 0)
                return 1;
    }
    return 0;
}

/* bfin() - Sweep simulated end-game beans. */
int bfin(b)
int b[];
{
    int i;

    for (i = 0; i < HST; i++)
    {
        b[HST] += b[i];
        b[i] = 0;
    }
    for (i = 7; i < CST; i++)
    {
        b[CST] += b[i];
        b[i] = 0;
    }
    return 0;
}

/* bmov() - Sow on a simulated board. */
int bmov(b, i, p)
int b[];
int i;
int p;
{
    int cnt;
    int pos;
    int op;
    int st;
    int hs;
    int cs;

    if (!owned(i, p) || b[i] == 0)
        return 0;
    cnt = b[i];
    b[i] = 0;
    pos = i;
    while (cnt > 0)
    {
        pos++;
        if (pos >= TOT)
            pos = 0;
        if (!skip(pos, p))
        {
            b[pos]++;
            cnt--;
        }
    }

    st = store(p);
    if (owned(pos, p) && b[pos] == 1)
    {
        op = opp(pos);
        if (b[op] > 0)
        {
            b[st] += b[op] + 1;
            b[op] = 0;
            b[pos] = 0;
        }
    }
    hs = bsumh(b);
    cs = bsumc(b);
    if (hs == 0 || cs == 0)
    {
        bfin(b);
        return 0;
    }
    if (pos == st)
        return 2;
    return 1;
}

/* iabs(n) - Return absolute value. */
int iabs(n)
int n;
{
    if (n < 0)
        return -n;
    return n;
}

/* bord(b,i,p) - Immediate move ordering score. */
int bord(b, i, p)
int b[];
int i;
int p;
{
    int tb[TOT];
    int st;
    int old;
    int res;
    int gain;
    int dif;
    int val;

    if (!owned(i, p) || b[i] == 0)
        return -30000;
    bcopy(tb, b);
    st = store(p);
    old = tb[st];
    res = bmov(tb, i, p);
    gain = tb[st] - old;
    val = (gain * 100) + b[i];
    if (res == 2)
        val += 60;
    if (res == 0)
    {
        dif = tb[CST] - tb[HST];
        val += iabs(dif) * 20;
    }
    return val;
}

/* bscore() - Evaluate a simulated board for the computer. */
int bscore(b)
int b[];
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
    for (i = 0; i < HST; i++)
    {
        hs += b[i];
        if (b[i] > 0)
            hm++;
    }
    for (i = 7; i < CST; i++)
    {
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

/* ordmv() - Build a tactical move order. */
int ordmv(b, p, mv)
int b[];
int p;
int mv[];
{
    int i;
    int j;
    int n;
    int lo;
    int hi;
    int tmp;
    int va[6];

    n = 0;
    if (p == 0)
    {
        lo = 0;
        hi = HST;
    }
    else
    {
        lo = 7;
        hi = CST;
    }
    for (i = lo; i < hi; i++)
    {
        if (b[i] > 0)
        {
            mv[n] = i;
            va[n] = bord(b, i, p);
            n++;
        }
    }
    for (i = 0; i < n - 1; i++)
    {
        for (j = i + 1; j < n; j++)
        {
            if (va[j] > va[i])
            {
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

/* abeta() - Alpha-beta score for simulated play. */
int abeta(b, p, dep, alp, bet, ord)
int b[];
int p;
int dep;
int alp;
int bet;
int ord;
{
    int i;
    int n;
    int res;
    int val;
    int best;
    int mv[6];
    int nb[TOT];

    if (dep <= 0)
        return bscore(b);
    if (!blegal(b, p))
        return bscore(b);
    if (ord)
        n = ordmv(b, p, mv);
    else
    {
        n = 0;
        if (p == 0)
        {
            for (i = 0; i < HST; i++)
                if (b[i] > 0)
                {
                    mv[n] = i;
                    n++;
                }
        }
        else
        {
            for (i = 7; i < CST; i++)
                if (b[i] > 0)
                {
                    mv[n] = i;
                    n++;
                }
        }
    }

    if (p == 1)
        best = -30000;
    else
        best = 30000;

    if (p == 0)
    {
        for (i = 0; i < n; i++)
        {
            bcopy(nb, b);
            res = bmov(nb, mv[i], p);
            if (res == 2)
                val = abeta(nb, p, dep - 1, alp, bet, ord);
            else
                val = abeta(nb, 1, dep - 1, alp, bet, ord);
            if (val < best)
                best = val;
            if (best < bet)
                bet = best;
            if (alp >= bet)
                return best;
        }
    }
    else
    {
        for (i = 0; i < n; i++)
        {
            bcopy(nb, b);
            res = bmov(nb, mv[i], p);
            if (res == 2)
                val = abeta(nb, p, dep - 1, alp, bet, ord);
            else
                val = abeta(nb, 0, dep - 1, alp, bet, ord);
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

/* rpick() - Pick a random legal computer pit. */
int rpick()
{
    int i;
    int n;
    int pick;
    unsigned r;

    n = 0;
    for (i = 7; i < CST; i++)
        if (pit[i] > 0)
            n++;
    if (n == 0)
        return 7;
    r = x_rand();
    pick = r % n;
    for (i = 7; i < CST; i++)
    {
        if (pit[i] > 0)
        {
            if (pick == 0)
                return i;
            pick--;
        }
    }
    return 7;
}

/* nxsel(d) - Move selected human pit. */
int nxsel(d)
int d;
{
    int old;

    old = sel;
    sel += d;
    if (sel < 0)
        sel = PITS - 1;
    if (sel >= PITS)
        sel = 0;
    drpit(old, 0);
    drpit(sel, 1);
    return 0;
}

/* setsel(n) - Select human pit by number. */
int setsel(n)
int n;
{
    int old;

    if (n < 0 || n >= PITS)
        return 0;
    old = sel;
    sel = n;
    drpit(old, 0);
    drpit(sel, 1);
    return 0;
}

/* setlvl(n) - Change computer level during play. */
int setlvl(n)
int n;
{
    if (n > 2)
        n = 2;
    lvl = n;
    stat();
    if (lvl == 0)
        note("COMPUTER LEVEL E: EASY");
    else if (lvl == 1)
        note("COMPUTER LEVEL M: MEDIUM");
    else
        note("COMPUTER LEVEL H: HARD");
    return 0;
}

/* savu() - Save state before a player move. */
int savu()
{
    int i;

    for (i = 0; i < TOT; i++)
        upit[i] = pit[i];
    usel = sel;
    uok = 1;
    return 0;
}

/* restu() - Restore the saved player move. */
int restu()
{
    int i;

    if (!uok)
    {
        note("NOTHING TO UNDO");
        return 0;
    }
    for (i = 0; i < TOT; i++)
        pit[i] = upit[i];
    sel = usel;
    over = 0;
    escst = 0;
    uok = 0;
    drpos();
    note("LAST MOVE UNDONE");
    return 0;
}

/* human() - Process the human turn. */
int human()
{
    int k;
    int c;
    int res;

    note("YOUR TURN: CHOOSE A PIT WITH BEANS");
    while (!quit && !over)
    {
        c = x_keyrd();
        k = keymap(c);
        if (c >= '1' && c <= '6')
        {
            setsel(c - '1');
            continue;
        }
        if (c == 'E' || c == 'e')
        {
            setlvl(0);
            continue;
        }
        if (c == 'M' || c == 'm')
        {
            setlvl(1);
            continue;
        }
        if (c == 'H' || c == 'h')
        {
            setlvl(2);
            continue;
        }
        if (c == '?' || c == 'T' || c == 't')
        {
            hshow();
            continue;
        }
        if (c == 'U' || c == 'u')
        {
            restu();
            continue;
        }
        if (k == KNON)
            continue;
        if (k == KQUI)
        {
            quit = 1;
            return 0;
        }
        if (k == KLT)
            nxsel(-1);
        else if (k == KRT)
            nxsel(1);
        else if (k == KPLC)
        {
            if (pit[sel] == 0)
                note("THAT PIT IS EMPTY");
            else
            {
                savu();
                res = move(sel, 0);
                if (res == 2 && !over)
                    note("YOU LANDED IN YOUR STORE: GO AGAIN");
                else
                    return 0;
            }
        }
    }
    return 0;
}

/* cpick() - Pick the computer's pit. */
int cpick()
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
    unsigned r;

    r = x_rand();
    if (lvl == 0 && (r % 4) == 0)
        return rpick();

    if (lvl == 0)
        dep = 2;
    else if (lvl == 1)
        dep = 3;
    else
        dep = 5;
    if (lvl >= 2)
        ord = 1;
    else
        ord = 0;

    best = 7;
    bval = -30000;
    alp = -30000;
    if (ord)
        n = ordmv(pit, 1, mv);
    else
    {
        n = 0;
        for (i = 7; i < CST; i++)
        {
            if (pit[i] > 0)
            {
                mv[n] = i;
                n++;
            }
        }
    }
    for (j = 0; j < n; j++)
    {
        i = mv[j];
        bcopy(nb, pit);
        res = bmov(nb, i, 1);
        if (res == 2)
            val = abeta(nb, 1, dep - 1, alp, 30000, ord);
        else
            val = abeta(nb, 0, dep - 1, alp, 30000, ord);
        val += bord(pit, i, 1) / 10;
        if (val > bval)
        {
            bval = val;
            best = i;
        }
        if (val > alp)
            alp = val;
    }
    return best;
}

/* hpick() - Pick a hard hint for the human. */
int hpick()
{
    int i;
    int best;
    int bval;
    int val;
    int res;
    int nb[TOT];

    best = -1;
    bval = 30000;
    for (i = 0; i < HST; i++)
    {
        if (pit[i] == 0)
            continue;
        bcopy(nb, pit);
        res = bmov(nb, i, 0);
        if (res == 2)
            val = abeta(nb, 0, 4, -30000, bval, 1);
        else
            val = abeta(nb, 1, 4, -30000, bval, 1);
        val -= bord(pit, i, 0) / 10;
        if (val < bval)
        {
            bval = val;
            best = i;
        }
    }
    return best;
}

/* hshow() - Show a human hint. */
int hshow()
{
    int i;

    note("THINKING ABOUT A HINT...");
    i = hpick();
    if (i < 0)
        note("NO HINT: YOU HAVE NO LEGAL MOVE");
    else
    {
        setsel(i);
        note("HINT: TRY THE HIGHLIGHTED PIT");
    }
    return 0;
}

/* pause() - Small thinking pause. */
int pause()
{
    int i;
    int j;

    for (i = 0; i < 250; i++)
        for (j = 0; j < 120; j++)
            ;
    return 0;
}

/* comp() - Run computer turns. */
int comp()
{
    int i;
    int res;

    while (!quit && !over)
    {
        note("COMPUTER IS THINKING...");
        pause();
        i = cpick();
        note("COMPUTER SOWS");
        res = move(i, 1);
        if (res == 2 && !over)
        {
            note("COMPUTER LANDED IN ITS STORE: AGAIN");
            pause();
        }
        else
            return 0;
    }
    return 0;
}

/* final() - Show winner. */
int final()
{
    clrnot(26);
    clrnot(27);
    clrnot(28);
    x_setc(37);
    x_curmv(26, 5);
    if (quit)
        pstr("GAME QUIT");
    else if (pit[HST] > pit[CST])
        pstr("YOU WIN! MORE BEANS IN YOUR STORE");
    else if (pit[HST] < pit[CST])
        pstr("COMPUTER WINS THIS HARVEST");
    else
        pstr("DRAW GAME");
    x_curmv(27, 5);
    pstr("FINAL  YOU:");
    num2(pit[HST]);
    pstr("  CPU:");
    num2(pit[CST]);
    x_rstc();
    return 0;
}

/* again() - Ask whether to start another game. */
int again()
{
    int c;

    if (quit)
        return 0;
    clrnot(28);
    x_setc(37);
    x_curmv(28, 5);
    pstr("NEW GAME?  Y/N");
    x_rstc();
    while (1)
    {
        c = x_keyrd();
        if (c == 'Y' || c == 'y')
            return 1;
        if (c == 'N' || c == 'n' || c == KEYQ || c == KEYq ||
            c == XK_ESC || x_iscc(c))
            return 0;
    }
    return 0;
}

/* main() - Game entry point. */
int main()
{
    int olvl;

    init();
    while (!quit)
    {
        drall();
        while (!quit && !over)
        {
            if (legal(0))
                human();
            if (!quit && !over && legal(1))
                comp();
            if (!legal(0) || !legal(1))
            {
                colend();
                drpos();
            }
        }
        final();
        if (!again())
            break;
        olvl = lvl;
        init();
        lvl = olvl;
    }

    x_curmv(SCRH, 1);
    x_shwcr();
    x_rstc();
    pstr("\r\n");
    return 0;
}
