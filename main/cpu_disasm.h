/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#pragma once

#include <stddef.h>
#include <stdint.h>

// CPU disassembler + shared monitor utilities.
//
// The concrete instruction-name table is chosen at compile time:
//   * ALTAIR_CPU_Z80 defined  -> Zilog Z80 mnemonics  (z80_disasm.c)
//   * ALTAIR_CPU_Z80 undefined -> Intel 8080 mnemonics (i8080_disasm.c)
// Both files are compiled; each guards its body on ALTAIR_CPU_Z80 so exactly
// one provides the symbols below.

// Convert uint8 to an 8-character binary string (buffer must be >= 9 bytes).
void uint8_to_binary(uint8_t value, char* buffer, size_t buffer_size);

// Publish a message to the WebSocket console (and VT100 display when present).
void publish_message(const char* message, size_t length);

// Disassemble a single base opcode into a mnemonic for the built-for CPU and
// report its instruction length in bytes via *instruction_length.
const char* get_cpu_instruction_name(uint8_t opcode, uint8_t* instruction_length);
