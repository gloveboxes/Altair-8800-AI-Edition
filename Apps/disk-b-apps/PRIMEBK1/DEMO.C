#include <stdio.h>
#include <string.h>
#include "BANKMEM.H"
#include "BIOSBANK.H"

extern unsigned char bcode[];
extern unsigned char bfix[];
extern unsigned char bnext[];
extern unsigned char bnfix[];
extern unsigned char bsub[];
extern unsigned char bend[];

/* Two ordinary stack-entry functions share a helper. Their sole argument is
 * a bank-1 context pointer at SP+2. Byte 0 is the input; byte 1 is count or
 * next prime; byte 2 is the last prime for the count function. */
#asm
        public _bcode
        public _bfix
        public _bnext
        public _bnfix
        public _bsub
        public _bend
_bcode:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)            ; first DCC pointer argument
        ld h,(ix+5)
        push hl
        pop iy
        ld c,(iy+0)            ; upper limit (0..255)
        ld d,2                 ; candidate
        ld e,0                 ; count
BC_LOOP:
        ld a,d
        cp c
        jr c,BC_TRY
        jr z,BC_TRY
        jr BC_DONE
BC_TRY:
        ld a,d
        db 0cdh                ; CALL relocated _bsub
_bfix:
        dw 0
        or a
        jr z,BC_NEXT
        inc e
        ld a,d
        ld (iy+2),a            ; last prime found
BC_NEXT:
        inc d
        jr nz,BC_LOOP          ; 255 wraps to zero: done
BC_DONE:
        ld a,e
        ld (iy+1),a            ; count
        ld l,a                 ; also return count in HL
        ld h,0
        pop ix
        ret
_bnext:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)
        ld h,(ix+5)
        push hl
        pop iy
        ld d,(iy+0)            ; seek strictly after input
BN_LOOP:
        inc d
        jr z,BN_NONE           ; no larger 8-bit candidate
        ld a,d
        db 0cdh                ; CALL relocated _bsub
_bnfix:
        dw 0
        or a
        jr z,BN_LOOP
        ld a,d
        jr BN_DONE
BN_NONE:
        xor a                 ; zero means no next prime <=255
BN_DONE:
        ld (iy+1),a
        ld l,a
        ld h,0
        pop ix
        ret
_bsub:
        push bc                ; nested CALL and PUSH use the selected stack
        ld c,a                 ; candidate
        cp 2
        jr c,BC_COMPOSITE
        ld b,2                 ; divisor
BC_DIV:
        ld a,b
        cp c
        jr z,BC_PRIME
        ld a,c
BC_MOD:
        sub b                  ; trial division by repeated subtraction
        jr z,BC_COMPOSITE
        jr nc,BC_MOD
        inc b
        jr BC_DIV
BC_PRIME:
        ld a,1
        pop bc
        ret
BC_COMPOSITE:
        xor a
        pop bc
        ret
_bend:
#endasm

static int check_case(bank_memory_handle_t code, bank_memory_handle_t stack, bank_memory_handle_t context,
                      unsigned int entry, unsigned int input,
                      unsigned int output, unsigned int last, int next)
{
    unsigned char data[16];
    unsigned char stack_tail[16];
    unsigned int result;

    if (stack)
    {
        memset(stack_tail, 0xA5, sizeof(stack_tail));
        if (!bank_memory_write(stack, 512 - sizeof(stack_tail), stack_tail, sizeof(stack_tail)))
            return 0;
    }
    result = bank_bios_call(code, stack, entry, context);
    if (!bank_memory_read(context, 0, data, sizeof(data)))
        return 0;
    if (!bank_memory_read(stack, 512 - sizeof(stack_tail), stack_tail, sizeof(stack_tail)) ||
        (stack_tail[14] == 0xA5 && stack_tail[15] == 0xA5))
        return 0;
    if (next)
        printf("Next after %u: %u; HL=%u\n", input, (unsigned int)data[1], result);
    else
        printf("Count <= %u: %u primes; last=%u; HL=%u\n",
               input, (unsigned int)data[1], (unsigned int)data[2], result);
    return data[0] == input && data[1] == output &&
           data[2] == last && result == output;
}

int main(void)
{
    bank_memory_handle_t code, stack_a, stack_b, context_a, context_b;
    unsigned int code_bytes, fix_offset, next_fix, next_entry, sub_address, free_before;
    unsigned char image[192], data[16];
    int passed, freed;

    code = stack_a = stack_b = context_a = context_b = 0;
    passed = 0;
    freed = 1;
    code_bytes = (unsigned int)(bend - bcode);
    fix_offset = (unsigned int)(bfix - bcode);
    next_fix = (unsigned int)(bnfix - bcode);
    next_entry = (unsigned int)(bnext - bcode);
    printf("BANKCODE: two bank-1 C-style entry points (%u code bytes).\n", code_bytes);
    if (code_bytes > sizeof(image) || fix_offset + 1 >= code_bytes ||
        next_fix + 1 >= code_bytes || next_entry >= code_bytes ||
        (unsigned int)(bsub - bcode) >= code_bytes)
    {
        puts("FAIL: embedded prime code does not fit the relocation buffer.");
        return 1;
    }
    if (!bank_memory_initialize())
    {
        puts("FAIL: banked SRAM/BIOS unavailable.");
        return 1;
    }

    free_before = bank_memory_available();
    code = bank_memory_allocate(code_bytes);
    stack_a = bank_memory_allocate(512);
    stack_b = bank_memory_allocate(512);
    context_a = bank_memory_allocate(16);
    context_b = bank_memory_allocate(16);
    if (!code || !stack_a || !stack_b || !context_a || !context_b)
    {
        puts("FAIL: could not allocate code, stacks and contexts.");
        goto cleanup;
    }
    memcpy(image, bcode, code_bytes);
    sub_address = code + (unsigned int)(bsub - bcode);
    image[fix_offset] = (unsigned char)sub_address;
    image[fix_offset + 1] = (unsigned char)(sub_address >> 8);
    image[next_fix] = (unsigned char)sub_address;
    image[next_fix + 1] = (unsigned char)(sub_address >> 8);
    if (!bank_memory_write(code, 0, image, code_bytes))
    {
        puts("FAIL: could not copy bank-1 prime code.");
        goto cleanup;
    }
    memset(data, 0, sizeof(data));
    data[0] = 30;
    if (!bank_memory_write(context_a, 0, data, sizeof(data)))
        goto cleanup;
    data[0] = 30;
    if (!bank_memory_write(context_b, 0, data, sizeof(data)))
        goto cleanup;

    if (!check_case(code, stack_a, context_a, 0, 30, 10, 29, 0) ||
        !check_case(code, stack_b, context_b, next_entry, 30, 31, 0, 1))
    {
        puts("FAIL: direct entry, prime result or DCC stack argument.");
        goto cleanup;
    }
    data[0] = 50;
    if (!bank_memory_write(context_a, 0, data, 1) || !bank_memory_write(context_b, 0, data, 1) ||
        !check_case(code, stack_a, context_a, 0, 50, 15, 47, 0) ||
        !check_case(code, stack_b, context_b, next_entry, 50, 53, 0, 1))
    {
        puts("FAIL: reused entry points returned incorrect results.");
        goto cleanup;
    }
    data[0] = 254;
    if (!bank_memory_write(context_b, 0, data, 1) ||
        !check_case(code, stack_b, context_b, next_entry, 254, 0, 0, 1) ||
        !bank_memory_read(context_a, 0, data, sizeof(data)) ||
        data[0] != 50 || data[1] != 15 || data[2] != 47)
    {
        puts("FAIL: no-next-prime case or context isolation.");
        goto cleanup;
    }
    passed = 1;

cleanup:
    if (context_b && !bank_memory_free(context_b)) freed = 0;
    if (context_a && !bank_memory_free(context_a)) freed = 0;
    if (stack_b && !bank_memory_free(stack_b)) freed = 0;
    if (stack_a && !bank_memory_free(stack_a)) freed = 0;
    if (code && !bank_memory_free(code)) freed = 0;
    if (!freed || bank_memory_available() != free_before)
    {
        puts("FAIL: bank-1 heap was not restored.");
        return 1;
    }
    if (!passed)
        return 1;
    puts("PASS: direct offsets entered two bank-1 functions with DCC stack arguments.");
    return 0;
}
