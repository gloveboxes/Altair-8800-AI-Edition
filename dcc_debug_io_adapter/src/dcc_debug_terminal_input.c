#include "dcc_debug_terminal_input.h"

#include <string.h>

#define ESCAPE_GRACE_MS 30u
#define CONTROL_KEY(character) ((uint8_t)((character) & 0x1f))

static uint8_t process_character(dcc_debug_terminal_input_t *terminal,
                                 uint8_t character, uint64_t now_ms)
{
    switch (terminal->state)
    {
        case DCC_DEBUG_TERMINAL_NORMAL:
            if (character == 0)
                return 0;
            if (character == 0x1b)
            {
                terminal->state = DCC_DEBUG_TERMINAL_ESCAPE;
                terminal->escape_start_ms = now_ms;
                return 0;
            }
            if (character == 0x7f || character == 0x08)
                return CONTROL_KEY('H');
            return character;

        case DCC_DEBUG_TERMINAL_ESCAPE:
            if (character == 0)
            {
                if (now_ms - terminal->escape_start_ms >= ESCAPE_GRACE_MS)
                {
                    terminal->state = DCC_DEBUG_TERMINAL_NORMAL;
                    return 0x1b;
                }
                return 0;
            }
            if (character == '[')
            {
                terminal->state = DCC_DEBUG_TERMINAL_ESCAPE_BRACKET;
                return 0;
            }
            terminal->state = DCC_DEBUG_TERMINAL_NORMAL;
            return character;

        case DCC_DEBUG_TERMINAL_ESCAPE_BRACKET:
            if (character == 0)
                return 0;
            terminal->state = DCC_DEBUG_TERMINAL_NORMAL;
            switch (character)
            {
                case 'A': return CONTROL_KEY('E');
                case 'B': return CONTROL_KEY('X');
                case 'C': return CONTROL_KEY('D');
                case 'D': return CONTROL_KEY('S');
                case '2': terminal->pending_key = CONTROL_KEY('O'); break;
                case '3': terminal->pending_key = CONTROL_KEY('G'); break;
                case '5': terminal->pending_key = CONTROL_KEY('R'); break;
                case '6': terminal->pending_key = CONTROL_KEY('V'); break;
                default: return 0;
            }
            terminal->state = DCC_DEBUG_TERMINAL_ESCAPE_BRACKET_NUMBER;
            return 0;

        case DCC_DEBUG_TERMINAL_ESCAPE_BRACKET_NUMBER:
            if (character == 0)
                return 0;
            terminal->state = DCC_DEBUG_TERMINAL_NORMAL;
            if (character == '~')
            {
                uint8_t result = terminal->pending_key;
                terminal->pending_key = 0;
                return result;
            }
            terminal->pending_key = 0;
            return 0;
    }
    terminal->state = DCC_DEBUG_TERMINAL_NORMAL;
    return 0;
}

void dcc_debug_terminal_input_init(dcc_debug_terminal_input_t *terminal)
{
    memset(terminal, 0, sizeof(*terminal));
}

size_t dcc_debug_terminal_input_process(
    dcc_debug_terminal_input_t *terminal,
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_size,
    uint64_t now_ms)
{
    size_t input_index;
    size_t output_count = 0;

    for (input_index = 0; input_index < input_size; ++input_index)
    {
        uint8_t translated = process_character(terminal, input[input_index], now_ms);
        if (translated != 0 && output_count < output_size)
            output[output_count++] = translated;
    }
    return output_count;
}

size_t dcc_debug_terminal_input_poll(
    dcc_debug_terminal_input_t *terminal,
    uint8_t *output, size_t output_size,
    uint64_t now_ms)
{
    uint8_t translated = process_character(terminal, 0, now_ms);
    if (translated == 0 || output_size == 0)
        return 0;
    output[0] = translated;
    return 1;
}