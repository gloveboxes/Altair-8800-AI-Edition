#include "HOSTCHK.H"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BANKLIB.H"
#include "BIOSBANK.H"

/* Preserve a bank-0 directory search across bank-1 library file activity. */
static unsigned char search_fcb[36];
static unsigned char search_dma[128];
static unsigned int first_match;
static unsigned int free_before_calls;

static void report_memory(void)
{
    meminfo_t info;
    meminfo(&info);
    printf("Bank 0 DCC heap: %u bytes capacity, %u committed, %u growth; stack: %u bytes\n",
           info.heap_capacity, info.heap_committed, info.heap_growth, info.stack_size);
}

unsigned int hsopen(void)
{
    report_memory();
    memset(search_fcb, '?', 12);
    search_fcb[0] = 0;
    memset(search_fcb + 12, 0, 24);
    bdos(26, (int)search_dma);
    first_match = bdos(17, (int)search_fcb);
    free_before_calls = bank_bios_available();
    if (!bank_library_open("DATA.LIB"))
        return 0;
    return 1;
}

unsigned int hsclos(void)
{
    unsigned int next_match = bdos(18, (int)search_fcb);
    unsigned int unload_result = bank_library_close();
    unsigned int free_after_calls = bank_bios_available();

    if (unload_result != 1 || first_match > 3 || next_match > 3 ||
        free_after_calls != free_before_calls)
    {
        printf("search=%u/%u free=%u/%u unload=%u\n",
               first_match, next_match, free_after_calls,
               free_before_calls, unload_result);
        return 0;
    }
    return 1;
}
