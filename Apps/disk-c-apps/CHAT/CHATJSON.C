#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "CHATJSON.H"

/* Append plain string to buffer with bounds checking */
static int json_append(char *buffer, size_t *position, size_t buffer_size, const char *text)
{
    size_t write_position = *position;

    while (*text != '\0') {
        if (write_position >= buffer_size - 1) {
            buffer[write_position] = '\0';
            return -1;
        }
        buffer[write_position++] = *text++;
    }

    buffer[write_position] = '\0';
    *position = write_position;
    return 0;
}

/* Append escaped string to buffer with bounds checking */
static int json_append_escaped(char *buffer, size_t *position, size_t buffer_size, const char *text)
{
    size_t write_position = *position;

    while (*text != '\0') {
        uint8_t character_value = (uint8_t)*text++ & 0x7f;
        char escape = '\0';

        if (character_value == '"' || character_value == '\\')
            escape = (char)character_value;
        else if (character_value == '\n')
            escape = 'n';
        else if (character_value == '\r')
            escape = 'r';
        else if (character_value == '\t')
            escape = 't';

        if (escape != '\0') {
            if (write_position >= buffer_size - 2) {
                buffer[write_position] = '\0';
                return -1;
            }
            buffer[write_position++] = '\\';
            buffer[write_position++] = escape;
        } else {
            if (character_value < ' ')
                character_value = ' ';
            if (write_position >= buffer_size - 1) {
                buffer[write_position] = '\0';
                return -1;
            }
            buffer[write_position++] = (char)character_value;
        }
    }

    buffer[write_position] = '\0';
    *position = write_position;
    return 0;
}

int j_genr(const char *system_message, const uint8_t *types, char *const *texts,
           size_t message_count, const char *model, const char *max_tokens,
           const char *temperature, char *output, size_t output_size)
{
    size_t position = 0;

    if (output_size == 0)
        return -1;
    output[0] = '\0';
    
    /* Build JSON using bounded append helpers */
    if (json_append(output, &position, output_size, "{\"model\":\"") < 0)
        return -1;
    if (json_append(output, &position, output_size, model) < 0)
        return -1;
    if (json_append(output, &position, output_size, "\",\"messages\":[") < 0)
        return -1;
    if (json_append(output, &position, output_size, "{\"role\":\"system\",\"content\":\"") < 0)
        return -1;
    if (json_append_escaped(output, &position, output_size, system_message) < 0)
        return -1;
    if (json_append(output, &position, output_size, "\"}") < 0)
        return -1;

    for (size_t index = 0; index < message_count; index++) {
        size_t previous_position = position;
        const char *message_text = texts[index] != NULL ? texts[index] : "";
        const char *role = types[index] == MSG_AST ? "assistant" : "user";

        if (json_append(output, &position, output_size, ",{\"role\":\"") < 0 ||
            json_append(output, &position, output_size, role) < 0 ||
            json_append(output, &position, output_size, "\",\"content\":\"") < 0 ||
            json_append_escaped(output, &position, output_size, message_text) < 0 ||
            json_append(output, &position, output_size, "\"}") < 0) {
            position = previous_position;
            output[position] = '\0';
        }
    }

    if (json_append(output, &position, output_size, "],\"max_tokens\":") < 0)
        return -1;
    if (json_append(output, &position, output_size, max_tokens) < 0)
        return -1;
    if (json_append(output, &position, output_size, ",\"temperature\":") < 0)
        return -1;
    if (json_append(output, &position, output_size, temperature) < 0)
        return -1;
    if (json_append(output, &position, output_size, ",\"stream\":true}") < 0)
        return -1;

    return (int)position;
}
