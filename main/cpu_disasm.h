/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#pragma once

#include <stddef.h>
#include <stdint.h>

// Z80 disassembler + shared monitor utilities.

// Convert uint8 to an 8-character binary string (buffer must be >= 9 bytes).
void uint8_to_binary(uint8_t value, char* buffer, size_t buffer_size);

// Publish a message to the WebSocket console (and VT100 display when present).
void publish_message(const char* message, size_t length);

// Disassemble a single base opcode into a mnemonic for the built-for CPU and
// report its instruction length in bytes via *instruction_length.
const char* get_cpu_instruction_name(uint8_t opcode, uint8_t* instruction_length);
