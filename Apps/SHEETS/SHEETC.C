/*
 * SHEETC.C - formula engine for SHEETS, split out from SHEETS.C so
 * each translation unit fits the BDS C 1.6 compiler (the combined
 * source overflowed the part-I parser). This module holds the cell
 * storage helpers, the formula parser/evaluator, and cell rendering.
 *
 * Shared state and externals live in SHEETS.H. stdio.h must remain
 * the first include so the BDS C COMMON external layout matches
 * SHEETS.C.
 */

#include "stdio.h"
#include "string.h"
#include "sheets.h"

/* ---- Cell helpers ---- */

/* Return 1 if formula string 's' uses the volatile RAND() function
 * somewhere, so setcel knows to freeze it to a fixed value. */
int hasrnd(s)
char *s;
{
    while (*s)
    {
        if (upr(s[0]) == 'R' && upr(s[1]) == 'A'
            && upr(s[2]) == 'N' && upr(s[3]) == 'D'
            && s[4] == '(')
            return 1;
        s++;
    }
    return 0;
}

int setcel(r, c, s)
int r;
int c;
char *s;
{
    char *p;
    char *sav;
    char lv[4];
    char nbuf[16];
    int n;
    int ok;

    if (r < 0 || r >= MAXROW || c < 0 || c >= MAXCOL)
        return -1;

    if (cells[r][c])
    {
        free(cells[r][c]);
        cells[r][c] = 0;
    }

    if (s == 0 || s[0] == 0)
        return 0;

    /* Trim leading and trailing whitespace so " =3+1 " stores as
     * "=3+1" and is recognised as a formula (detection keys on the
     * first character). A blank/all-space entry clears the cell. */
    while (*s == ' ' || *s == '\t')
        s++;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        n--;
    if (n == 0)
        return 0;

    /* Freeze volatile RAND(): unlike Excel's RAND() (which re-rolls
     * on every recalc), this grid re-evaluates every visible cell
     * on each cursor move, so a stored "=RAND()" would change as you
     * scroll over it. Evaluate the formula once here and store the
     * resulting integer, so the random value is fixed when entered
     * (like typing =RAND() in Excel then pressing F9 / paste-values).
     * If evaluation fails the formula is kept and renders #ERR. */
    if (s[0] == '=' && hasrnd(s))
    {
        edepth = 0;
        sav = epos;
        epos = s + 1;
        eok = 1;
        ok = expr(lv);
        epos = sav;
        if (ok && eok)
        {
            ltoa(nbuf, lv);
            s = nbuf;
            n = strlen(s);
        }
    }

    p = alloc(n + 1);
    if (p == 0)
        return -1;
    strncpy(p, s, n);
    p[n] = 0;
    cells[r][c] = p;
    return 0;
}

int clrcel(r, c)
int r;
int c;
{
    if (cells[r][c])
    {
        free(cells[r][c]);
        cells[r][c] = 0;
        dirty = 1;
    }
    return 0;
}

/* ---- Parser / evaluator ---- */

int eskp()
{
    while (*epos == ' ' || *epos == '\t')
        epos++;
    return 0;
}

int isdig(c)
int c;
{
    return (c >= '0' && c <= '9');
}

int isal(c)
int c;
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int upr(c)
int c;
{
    if (c >= 'a' && c <= 'z')
        return c - 32;
    return c;
}

/* parse a cell ref like A1 / z99, optionally with Excel-style
 * absolute markers ($A$1, $A1, A$1); the '$' signs are accepted
 * and skipped here (they only matter when relocating formulas).
 * Set *rp, *cp, advance epos. */
int prsref(rp, cp)
int *rp;
int *cp;
{
    int col;
    int row;

    eskp();
    if (*epos == '$')
        epos++;
    if (!isal(*epos))
        return 0;
    col = upr(*epos) - 'A';
    epos++;
    if (*epos == '$')
        epos++;
    if (!isdig(*epos))
        return 0;
    row = 0;
    while (isdig(*epos))
    {
        row = row * 10 + (*epos - '0');
        epos++;
    }
    row = row - 1;
    if (row < 0 || row >= MAXROW || col < 0 || col >= MAXCOL)
        return 0;
    *rp = row;
    *cp = col;
    return 1;
}

int evcell();
int expr();
int term();
int factor();

/* Read a 16-bit value from the emulator hardware RNG: pulse port
 * 45 to generate, then read the low and high bytes from port 200.
 * Masked to 0..32767 so the result is always a non-negative int. */
int rndnum()
{
    int r;

    outp(45, 1);
    r = inp(200) & 255;
    r = r | ((inp(200) & 255) << 8);
    return r & 0x7FFF;
}

/* Forward-call wrapper: evaluate cell (r,c) into the 4-byte long
 * pointed to by vp. Recursion-guarded via edepth. */
int evcell(r, c, vp)
int r;
int c;
char *vp;
{
    char *s;
    char *sav;
    int ok;

    itol(vp, 0);
    if (r < 0 || r >= MAXROW || c < 0 || c >= MAXCOL)
        return 0;
    s = cells[r][c];
    if (s == 0 || s[0] == 0)
        return 1;
    if (s[0] != '=')
    {
        /* Plain text/number: take leading signed integer if any. */
        if (s[0] == '-' || isdig(s[0]))
            atol(vp, s);
        return 1;
    }
    if (edepth > 24)
        return 0;
    edepth++;
    sav = epos;
    epos = s + 1;
    eok = 1;
    ok = expr(vp);
    epos = sav;
    edepth--;
    if (!ok || !eok)
        return 0;
    return 1;
}

int factor(vp)
char *vp;
{
    int neg;
    int r, c, r2, c2;
    int i, j;
    int tag;
    int cnt;
    int gotn;
    int rn;
    char rv[4];
    char dig[4];
    char tmp[4];

    eskp();
    neg = 0;
    while (*epos == '-' || *epos == '+')
    {
        if (*epos == '-')
            neg = !neg;
        epos++;
        eskp();
    }

    if (*epos == '(')
    {
        epos++;
        if (!expr(vp))
            return 0;
        eskp();
        if (*epos != ')')
        {
            eok = 0;
            return 0;
        }
        epos++;
    }
    else if (isdig(*epos))
    {
        itol(vp, 0);
        while (isdig(*epos))
        {
            itol(dig, *epos - '0');
            lmul(vp, vp, lten);
            ladd(vp, vp, dig);
            epos++;
        }
    }
    else if ((upr(epos[0]) == 'R') && (upr(epos[1]) == 'A')
             && (upr(epos[2]) == 'N') && (upr(epos[3]) == 'D')
             && (epos[4] == '('))
    {
        /* =RAND()  -> random 0..32767 (hardware RNG port).
         * =RAND(n) -> random 0..n-1 for n > 0. Frozen to a fixed
         * value when entered (see setcel), unlike Excel RAND(). */
        epos = epos + 5;
        eskp();
        if (*epos == ')')
        {
            epos++;
            itol(vp, rndnum());
        }
        else
        {
            if (!expr(rv))
                return 0;
            eskp();
            if (*epos != ')')
            {
                eok = 0;
                return 0;
            }
            epos++;
            rn = ltoi(rv);
            if (rn <= 0)
                itol(vp, rndnum());
            else
                itol(vp, rndnum() % rn);
        }
    }
    else if ((upr(epos[0]) == 'S') && (upr(epos[1]) == 'U')
             && (upr(epos[2]) == 'M') && (epos[3] == '('))
    {
        tag = 0;
        epos = epos + 4;
        goto rng;
    }
    else if ((upr(epos[0]) == 'A') && (upr(epos[1]) == 'V')
             && (upr(epos[2]) == 'G') && (epos[3] == '('))
    {
        tag = 1;
        epos = epos + 4;
        goto rng;
    }
    else if ((upr(epos[0]) == 'M') && (upr(epos[1]) == 'I')
             && (upr(epos[2]) == 'N') && (epos[3] == '('))
    {
        tag = 2;
        epos = epos + 4;
        goto rng;
    }
    else if ((upr(epos[0]) == 'M') && (upr(epos[1]) == 'A')
             && (upr(epos[2]) == 'X') && (epos[3] == '('))
    {
        tag = 3;
        epos = epos + 4;
        goto rng;
    }
    else if ((upr(epos[0]) == 'C') && (upr(epos[1]) == 'O')
             && (upr(epos[2]) == 'U') && (upr(epos[3]) == 'N')
             && (upr(epos[4]) == 'T') && (epos[5] == '('))
    {
        tag = 4;
        epos = epos + 6;
rng:
        if (!prsref(&r, &c))
        {
            eok = 0;
            return 0;
        }
        eskp();
        if (*epos != ':')
        {
            eok = 0;
            return 0;
        }
        epos++;
        if (!prsref(&r2, &c2))
        {
            eok = 0;
            return 0;
        }
        eskp();
        if (*epos != ')')
        {
            eok = 0;
            return 0;
        }
        epos++;
        itol(vp, 0);
        cnt = 0;
        gotn = 0;
        for (i = r; i <= r2; i++)
        {
            for (j = c; j <= c2; j++)
            {
                if (tag == 4)
                {
                    if (cells[i][j] && cells[i][j][0])
                        cnt++;
                    continue;
                }
                if (!evcell(i, j, rv))
                {
                    eok = 0;
                    return 0;
                }
                /* Only count cells that hold a numeric value (a
                 * number or a formula) so AVG ignores empty and
                 * non-numeric text cells. */
                if (cells[i][j] && (cells[i][j][0] == '='
                    || cells[i][j][0] == '-'
                    || isdig(cells[i][j][0])))
                    cnt++;
                if (tag == 0 || tag == 1)
                {
                    ladd(vp, vp, rv);
                }
                else if (tag == 2)
                {
                    if (!gotn || lcomp(rv, vp) < 0)
                    {
                        vp[0] = rv[0];
                        vp[1] = rv[1];
                        vp[2] = rv[2];
                        vp[3] = rv[3];
                    }
                    gotn = 1;
                }
                else if (tag == 3)
                {
                    if (!gotn || lcomp(rv, vp) > 0)
                    {
                        vp[0] = rv[0];
                        vp[1] = rv[1];
                        vp[2] = rv[2];
                        vp[3] = rv[3];
                    }
                    gotn = 1;
                }
            }
        }
        if (tag == 1)
        {
            if (cnt == 0)
            {
                eok = 0;
                return 0;
            }
            itol(dig, cnt);
            ldiv(vp, vp, dig);
        }
        else if (tag == 4)
        {
            itol(vp, cnt);
        }
    }
    else if (isal(*epos) || *epos == '$')
    {
        if (!prsref(&r, &c))
        {
            eok = 0;
            return 0;
        }
        if (!evcell(r, c, vp))
        {
            eok = 0;
            return 0;
        }
    }
    else
    {
        eok = 0;
        return 0;
    }

    if (neg)
    {
        lsub(tmp, lzro, vp);
        vp[0] = tmp[0];
        vp[1] = tmp[1];
        vp[2] = tmp[2];
        vp[3] = tmp[3];
    }
    return 1;
}

int term(vp)
char *vp;
{
    char rhs[4];
    char op;

    if (!factor(vp))
        return 0;
    eskp();
    while (*epos == '*' || *epos == '/')
    {
        op = *epos;
        epos++;
        if (!factor(rhs))
            return 0;
        if (op == '*')
            lmul(vp, vp, rhs);
        else
        {
            if (lcomp(rhs, lzro) == 0)
            {
                eok = 0;
                return 0;
            }
            ldiv(vp, vp, rhs);
        }
        eskp();
    }
    return 1;
}

int expr(vp)
char *vp;
{
    char rhs[4];
    char op;

    if (!term(vp))
        return 0;
    eskp();
    while (*epos == '+' || *epos == '-')
    {
        op = *epos;
        epos++;
        if (!term(rhs))
            return 0;
        if (op == '+')
            ladd(vp, vp, rhs);
        else
            lsub(vp, vp, rhs);
        eskp();
    }
    return 1;
}

/* ---- Formatting ---- */

/* Return 1 if formula string 's' contains a "#REF!" token left
 * behind by a relocation that pushed a reference off the grid. */
int hsref(s)
char *s;
{
    while (*s)
    {
        if (s[0] == '#' && s[1] == 'R' && s[2] == 'E'
            && s[3] == 'F' && s[4] == '!')
            return 1;
        s++;
    }
    return 0;
}

/* Render cell (r,c) into buf right-padded/truncated to CWID chars
 * plus a trailing NUL. Returns 0 always. */
int rndcel(r, c, buf)
int r;
int c;
char *buf;
{
    char *s;
    char tmp[16];
    char lv[4];
    int ok;
    int n, i, p;

    for (i = 0; i < CWID; i++)
        buf[i] = ' ';
    buf[CWID] = 0;

    s = cells[r][c];
    if (s == 0 || s[0] == 0)
        return 0;

    if (s[0] == '=')
    {
        if (hsref(s))
        {
            strcpy(buf, "  #REF!   ");
            buf[CWID] = 0;
            return 0;
        }
        edepth = 0;
        ok = evcell(r, c, lv);
        if (!ok)
        {
            strcpy(buf, "  #ERR    ");
            buf[CWID] = 0;
            return 0;
        }
        ltoa(tmp, lv);
        n = strlen(tmp);
        if (n > CWID)
        {
            for (i = 0; i < CWID; i++)
                buf[i] = '#';
            buf[CWID] = 0;
            return 0;
        }
        p = CWID - n;
        for (i = 0; i < n; i++)
            buf[p + i] = tmp[i];
        return 0;
    }

    /* Plain text or number: right-align numeric, left-align text. */
    if (s[0] == '-' || isdig(s[0]))
    {
        n = strlen(s);
        if (n > CWID)
        {
            for (i = 0; i < CWID; i++)
                buf[i] = '#';
            return 0;
        }
        p = CWID - n;
        for (i = 0; i < n; i++)
            buf[p + i] = s[i];
        return 0;
    }

    n = strlen(s);
    if (n > CWID)
        n = CWID;
    for (i = 0; i < n; i++)
        buf[i] = s[i];
    return 0;
}
