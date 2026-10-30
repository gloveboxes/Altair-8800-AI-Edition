#include <stdio.h>
#include <string.h>
#include "../PRIMEBK1/BIOSBANK.H"
#include "SPAPI.H"

/* Preserve a bank-0 directory search across bank-1 library file activity. */
/* clang-format off */
#ifndef __INTELLISENSE__
#asm
        public _bdocall
_bdocall:
        push ix
        ld ix,0
        add ix,sp
        ld c,(ix+4)
        ld e,(ix+6)
        ld d,(ix+7)
        call 0005h
        pop ix
        ret
#endasm
#endif
/* clang-format on */
unsigned int bdocall(unsigned int function, void *argument);

int main(void)
{
    unsigned char search_fcb[36], search_dma[128];
    unsigned int value, first, second, before;
    unsigned int first_match, next_match, unload_result;
    memset(search_fcb, '?', 12);
    search_fcb[0] = 0;
    memset(search_fcb + 12, 0, 24);
    bdocall(26, search_dma);
    first_match = bdocall(17, search_fcb);
    if (!spopen())
    {
        puts("FAIL: bank-1 library load or onload.");
        return 1;
    }
    before = bmavl();
    value = 5;
    first = veg(&value);
    second = marm(&value);
    next_match = bdocall(18, search_fcb);
    unload_result = spclos();
    if (unload_result != 1 || first != 12 || second != 23 ||
        value != 23 || first_match > 3 || next_match > 3 ||
        bmavl() != before)
    {
        printf("first=%u second=%u value=%u search=%u/%u free=%u/%u unload=%u\n",
               first, second, value, first_match, next_match,
               bmavl(), before, unload_result);
        puts("FAIL: bank-1 C calls or cleanup.");
        return 1;
    }
    printf("VEGEMITE=%u MARMITE=%u\n", first, second);
    puts("PASS: DCC C functions loaded from SPREADS.LIB and called by name.");
    return 0;
}
