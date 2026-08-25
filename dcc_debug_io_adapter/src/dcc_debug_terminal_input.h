#ifndef DCC_DEBUG_TERMINAL_INPUT_H
#define DCC_DEBUG_TERMINAL_INPUT_H

#include <stddef.h>
#include <stdint.h>

typedef enum dcc_debug_terminal_state
{
    DCC_DEBUG_TERMINAL_NORMAL,
    DCC_DEBUG_TERMINAL_ESCAPE,
    DCC_DEBUG_TERMINAL_ESCAPE_BRACKET,
    DCC_DEBUG_TERMINAL_ESCAPE_BRACKET_NUMBER
} dcc_debug_terminal_state_t;

typedef struct dcc_debug_terminal_input
{
    dcc_debug_terminal_state_t state;
    uint8_t pending_key;
    uint64_t escape_start_ms;
} dcc_debug_terminal_input_t;

void dcc_debug_terminal_input_init(dcc_debug_terminal_input_t *terminal);
size_t dcc_debug_terminal_input_process(
    dcc_debug_terminal_input_t *terminal,
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_size,
    uint64_t now_ms);
size_t dcc_debug_terminal_input_poll(
    dcc_debug_terminal_input_t *terminal,
    uint8_t *output, size_t output_size,
    uint64_t now_ms);

#endif