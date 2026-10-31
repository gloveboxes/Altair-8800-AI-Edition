#include <stdio.h>
#include <string.h>
#include "BANKMEM.H"
#include "BIOSBANK.H"

extern unsigned char bcode[];
extern unsigned char bmarm[];
extern unsigned char bend[];

/* One bank-1 parameter block, passed by reference through the DCC C stack.
 * requested is input; written and text are outputs. Char fields make the
 * assembly offsets explicit: requested=0, written=1, text=2. */
struct SpreadParams {
    unsigned char requested;
    unsigned char written;
    char text[9];
};

/* A future bank-1 linker could provide these bindings. Keep the BIOS call and
 * bank-0/bank-1 copies inside the wrappers, away from the example call sites. */
static struct {
    bank_memory_handle_t code;
    bank_memory_handle_t stack;
    bank_memory_handle_t params;
    unsigned int marmite_offset;
    unsigned int free_before;
} spread_module;

/* Both routines dereference the argument in bank 1 and have no absolute
 * references, so this code block needs no relocation fixups. */
/* clang-format off */
#ifndef __INTELLISENSE__
#asm
        public _bcode
        public _bmarm
        public _bend
_bcode:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)
        ld h,(ix+5)
        ld a,(hl)              ; requested character count
        inc hl                 ; written output
        cp 8
        jr c,BV_LENGTH
        ld a,8
BV_LENGTH:
        ld (hl),a
        inc hl                 ; text output
        ld (hl),'V'
        inc hl
        ld (hl),'E'
        inc hl
        ld (hl),'G'
        inc hl
        ld (hl),'E'
        inc hl
        ld (hl),'M'
        inc hl
        ld (hl),'I'
        inc hl
        ld (hl),'T'
        inc hl
        ld (hl),'E'
        ld l,a                 ; return written length in HL
        ld h,0
        pop ix
        ret
_bmarm:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)
        ld h,(ix+5)
        ld a,(hl)
        inc hl
        cp 7
        jr c,BM_LENGTH
        ld a,7
BM_LENGTH:
        ld (hl),a
        inc hl
        ld (hl),'M'
        inc hl
        ld (hl),'A'
        inc hl
        ld (hl),'R'
        inc hl
        ld (hl),'M'
        inc hl
        ld (hl),'I'
        inc hl
        ld (hl),'T'
        inc hl
        ld (hl),'E'
        ld l,a
        ld h,0
        pop ix
        ret
_bend:
#endasm
#endif
/* clang-format on */

static int spread_call(unsigned int offset, struct SpreadParams *params)
{
    int length;
    if (!bank_memory_write(spread_module.params, 0, params, sizeof(*params)))
        return -1;
    length = bank_bios_call(spread_module.code, spread_module.stack, offset,
                    spread_module.params);
    memset(params, 0xA5, sizeof(*params));
    if (!bank_memory_read(spread_module.params, 0, params, sizeof(*params)))
        return -1;
    return length;
}

static int vegemite(struct SpreadParams *params)
{
    return spread_call(0, params);
}

static int marmite(struct SpreadParams *params)
{
    return spread_call(spread_module.marmite_offset, params);
}

static int show_word(struct SpreadParams *params, int length,
                     unsigned int requested, const char *expected)
{
    unsigned int expected_length = strlen(expected);
    if (expected_length >= sizeof(params->text) ||
        params->requested != requested || params->written != expected_length ||
        length != expected_length ||
        memcmp(params->text, expected, expected_length))
        return 0;
    params->text[params->written] = 0;
    puts(params->text);
    return 1;
}

static int spreads_unload(void)
{
    int freed = 1;

    if (spread_module.params && !bank_memory_free(spread_module.params)) freed = 0;
    if (spread_module.stack && !bank_memory_free(spread_module.stack)) freed = 0;
    if (spread_module.code && !bank_memory_free(spread_module.code)) freed = 0;
    spread_module.code = spread_module.stack = spread_module.params = 0;
    return freed && bank_memory_available() == spread_module.free_before;
}

static int spreads_load(void)
{
    unsigned int code_bytes = (unsigned int)(bend - bcode);

    spread_module.code = spread_module.stack = spread_module.params = 0;
    spread_module.marmite_offset = (unsigned int)(bmarm - bcode);
    if (!code_bytes || spread_module.marmite_offset >= code_bytes || !bank_memory_initialize())
    {
        puts("FAIL: BIOS ABI 1.0 and matching bank-1 services required.");
        return 0;
    }

    spread_module.free_before = bank_memory_available();
    spread_module.code = bank_memory_allocate(code_bytes);
    spread_module.stack = bank_memory_allocate(256);
    spread_module.params = bank_memory_allocate(sizeof(struct SpreadParams));
    if (!spread_module.code || !spread_module.stack || !spread_module.params)
    {
        puts("FAIL: bank-1 allocation.");
        if (!spreads_unload())
            puts("FAIL: bank-1 heap was not restored.");
        return 0;
    }
    if (!bank_memory_write(spread_module.code, 0, bcode, code_bytes))
    {
        puts("FAIL: bank-1 code copy.");
        if (!spreads_unload())
            puts("FAIL: bank-1 heap was not restored.");
        return 0;
    }
    return 1;
}

int main(void)
{
    struct SpreadParams params;
    int length, passed = 0;

    puts("SPREADS: two bank-1 entry points.");
    if (!spreads_load())
        return 1;
    memset(&params, 0, sizeof(params));
    params.requested = 8;
    length = vegemite(&params);
    if (!show_word(&params, length, 8, "VEGEMITE"))
        goto done;
    params.requested = 7;
    length = marmite(&params);
    if (!show_word(&params, length, 7, "MARMITE"))
        goto done;
    params.requested = 3;
    length = vegemite(&params);
    if (!show_word(&params, length, 3, "VEG"))
        goto done;
    passed = 1;

done:
    if (!passed)
        puts("FAIL: entry point, DCC argument or result.");
    if (!spreads_unload())
    {
        puts("FAIL: bank-1 heap was not restored.");
        return 1;
    }
    if (!passed)
        return 1;
    puts("PASS: both entry points ran and the bank-1 heap was restored.");
    return 0;
}
