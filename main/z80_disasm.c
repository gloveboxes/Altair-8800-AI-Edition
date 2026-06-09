/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#include "cpu_disasm.h"

// Z80 disassembler. Active only when the firmware is built for the Z80 CPU.
// When built for the 8080, i8080_disasm.c provides these symbols instead.
#if defined(ALTAIR_CPU_Z80)

#include "websocket_console.h"
#include "sdkconfig.h"
#if CONFIG_ALTAIR_DISPLAY_AXS15231B
#include "vt100_terminal.h"
#endif

#include <stddef.h>
#include <string.h>
#include <stdio.h>

void uint8_to_binary(uint8_t bitmap, char* buffer, size_t buffer_length)
{
    uint16_t mask      = 1;
    uint8_t bit_number = 8;

    if (buffer_length < 9)
    {
        return;
    }

    while (bit_number-- > 0)
    {
        buffer[bit_number] = bitmap & mask ? '1' : '0';
        mask               = (uint16_t)(mask << 1);
    }

    buffer[8] = 0x00;
}

/**
 * @brief Publish message to WebSocket clients
 */
void publish_message(const char* message, size_t length)
{
    if (!message || length == 0)
    {
        return;
    }

    for (size_t i = 0; i < length; i++)
    {
        uint8_t c = (uint8_t)message[i];
        websocket_console_enqueue_output(c);
#if CONFIG_ALTAIR_DISPLAY_AXS15231B
        vt100_terminal_putchar(c);
#endif
    }
}

// Zilog Z80 base (unprefixed) opcode mnemonics.
//
// Sourced from the z80_instructions[] table in altair8800/x80.cxx (the ntvcm
// core's own disassembler). Operand markers ("d8", "d16", "a16") are kept so
// callers can substitute live operand bytes when rendering.
//
// The four Z80 prefix bytes (0xCB, 0xDD, 0xED, 0xFD) introduce multi-byte
// instructions; their entries are placeholders describing the prefix. Full
// decoding of prefixed opcodes is handled by the core's x80_render_operation().
static const char z80_instruction[256][16] = {
    /*00*/ "nop",        "ld bc, d16", "ld (bc), a",   "inc bc",      "inc b",        "dec b",      "ld b, d8",    "rlca",
    /*08*/ "ex af, af'", "add hl, bc", "ld a, (bc)",   "dec bc",      "inc c",        "dec c",      "ld c, d8",    "rrca",
    /*10*/ "djnz d8",    "ld de, d16", "ld (de), a",   "inc de",      "inc d",        "dec d",      "ld d, d8",    "rla",
    /*18*/ "jr d8",      "add hl, de", "ld a, (de)",   "dec de",      "inc e",        "dec e",      "ld e, d8",    "rra",
    /*20*/ "jr nz, d8",  "ld hl, d16", "ld (a16), hl", "inc hl",      "inc h",        "dec h",      "ld h, d8",    "daa",
    /*28*/ "jr z, d8",   "add hl, hl", "ld hl, (a16)", "dec hl",      "inc l",        "dec l",      "ld l, d8",    "cpl",
    /*30*/ "jr nc, d8",  "ld sp, d16", "ld (a16), a",  "inc sp",      "inc (hl)",     "dec (hl)",   "ld (hl), d8", "scf",
    /*38*/ "jr c, d8",   "add hl, sp", "ld a, (a16)",  "dec sp",      "inc a",        "dec a",      "ld a, d8",    "ccf",
    /*40*/ "ld b, b",    "ld b, c",    "ld b, d",      "ld b, e",     "ld b, h",      "ld b, l",    "ld b, (hl)",  "ld b, a",
    /*48*/ "ld c, b",    "ld c, c",    "ld c, d",      "ld c, e",     "ld c, h",      "ld c, l",    "ld c, (hl)",  "ld c, a",
    /*50*/ "ld d, b",    "ld d, c",    "ld d, d",      "ld d, e",     "ld d, h",      "ld d, l",    "ld d, (hl)",  "ld d, a",
    /*58*/ "ld e, b",    "ld e, c",    "ld e, d",      "ld e, e",     "ld e, h",      "ld e, l",    "ld e, (hl)",  "ld e, a",
    /*60*/ "ld h, b",    "ld h, c",    "ld h, d",      "ld h, e",     "ld h, h",      "ld h, l",    "ld h, (hl)",  "ld h, a",
    /*68*/ "ld l, b",    "ld l, c",    "ld l, d",      "ld l, e",     "ld l, h",      "ld l, l",    "ld l, (hl)",  "ld l, a",
    /*70*/ "ld (hl), b", "ld (hl), c", "ld (hl), d",   "ld (hl), e",  "ld (hl), h",   "ld (hl), l", "halt",        "ld (hl), a",
    /*78*/ "ld a, b",    "ld a, c",    "ld a, d",      "ld a, e",     "ld a, h",      "ld a, l",    "ld a, (hl)",  "ld a, a",
    /*80*/ "add a, b",   "add a, c",   "add a, d",     "add a, e",    "add a, h",     "add a, l",   "add a, (hl)", "add a, a",
    /*88*/ "adc a, b",   "adc a, c",   "adc a, d",     "adc a, e",    "adc a, h",     "adc a, l",   "adc a, (hl)", "adc a, a",
    /*90*/ "sub b",      "sub c",      "sub d",        "sub e",       "sub h",        "sub l",      "sub (hl)",    "sub a",
    /*98*/ "sbc a, b",   "sbc a, c",   "sbc a, d",     "sbc a, e",    "sbc a, h",     "sbc a, l",   "sbc a, (hl)", "sbc a, a",
    /*a0*/ "and b",      "and c",      "and d",        "and e",       "and h",        "and l",      "and (hl)",    "and a",
    /*a8*/ "xor b",      "xor c",      "xor d",        "xor e",       "xor h",        "xor l",      "xor (hl)",    "xor a",
    /*b0*/ "or b",       "or c",       "or d",         "or e",        "or h",         "or l",       "or (hl)",     "or a",
    /*b8*/ "cp b",       "cp c",       "cp d",         "cp e",        "cp h",         "cp l",       "cp (hl)",     "cp a",
    /*c0*/ "ret nz",     "pop bc",     "jp nz, a16",   "jp a16",      "call nz, a16", "push bc",    "add a, d8",   "rst 0",
    /*c8*/ "ret z",      "ret",        "jp z, a16",    "cb:",         "call z, a16",  "call a16",   "adc a, d8",   "rst 1",
    /*d0*/ "ret nc",     "pop de",     "jp nc, a16",   "out (d8), a", "call nc, a16", "push de",    "sub d8",      "rst 2",
    /*d8*/ "ret c",      "exx",        "jp c, a16",    "in a, (d8)",  "call c, a16",  "ix:",        "sbc d8",      "rst 3",
    /*e0*/ "ret po",     "pop hl",     "jp po, a16",   "ex (sp), hl", "call po, a16", "push hl",    "and d8",      "rst 4",
    /*e8*/ "ret pe",     "jp (hl)",    "jp pe, a16",   "ex de, hl",   "call pe, a16", "ed:",        "xor d8",      "rst 5",
    /*f0*/ "ret p",      "pop af",     "jp p, a16",    "di",          "call p, a16",  "push af",    "or d8",       "rst 6",
    /*f8*/ "ret m",      "ld sp, hl",  "jp m, a16",    "ei",          "call m, a16",  "iy:",        "cp d8",       "rst 7",
};

// Per-opcode length, in bytes, of the base (unprefixed) instruction.
// For the four prefix bytes (0xCB, 0xDD, 0xED, 0xFD) this is the length of the
// prefix byte alone; the following byte(s) carry the rest of the instruction.
static const uint8_t z80_instruction_length[256] = {
    /*00*/ 1, 3, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1,
    /*10*/ 2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1,
    /*20*/ 2, 3, 3, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1,
    /*30*/ 2, 3, 3, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1,
    /*40*/ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /*50*/ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /*60*/ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /*70*/ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /*80*/ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /*90*/ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /*a0*/ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /*b0*/ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /*c0*/ 1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 1, 3, 3, 2, 1,
    /*d0*/ 1, 1, 3, 2, 3, 1, 2, 1, 1, 1, 3, 2, 3, 1, 2, 1,
    /*e0*/ 1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, 1, 2, 1,
    /*f0*/ 1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, 1, 2, 1,
};

const char* get_cpu_instruction_name(uint8_t opcode, uint8_t* instruction_length) {
    if (instruction_length != NULL) {
        *instruction_length = z80_instruction_length[opcode];
    }
    return z80_instruction[opcode];
}

#endif // ALTAIR_CPU_Z80

