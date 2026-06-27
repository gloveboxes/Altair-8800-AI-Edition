#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * CHATC89 - dcc C89 CP/M 2.2 chat client for the Altair 8800 emulator.
 *
 * This is the C89/Z80 port of the BDS C CHAT app.  It keeps the OpenAI
 * compatible port protocol, migrates the CHATJSON request builder into this
 * file, and sends console output through a fully buffered stdout stream.
 *
 * Build from the dcc repo with this file as chatc89.c:
 *   ./ma.sh chatc89 peep        -> CHATC89.COM
 */

#define CHAT_VERSION "1.6-c89"

enum {
    MESSAGE_TYPE_USER = 1,
    MESSAGE_TYPE_ASSISTANT = 2,

    MESSAGE_HISTORY_LIMIT = 20,
    SYSTEM_PROMPT_LENGTH = 1024,
    USER_INPUT_LENGTH = 256,
    ASSISTANT_RESPONSE_LENGTH = 8192,
    REQUEST_BUFFER_LENGTH = 8192,
    CONFIG_VALUE_LENGTH = 16,
    MODEL_NAME_LENGTH = 64,
    ENV_VALUE_BUFFER_LENGTH = 128,

    WRAP_LINE_WIDTH = 80,
    WRAP_WORD_BUFFER_SIZE = 80,
    CONSOLE_BUFFER_SIZE = 1024,

    OPENAI_STATUS_EOF = 0,
    OPENAI_STATUS_DATA_READY = 2,

    ANSI_GREEN = 32,
    ANSI_YELLOW = 33,
    ANSI_CYAN = 36,

    ENV_VALUE_LENGTH = 256,
    ENV_STATUS_OK = 0,
    ENV_COMMAND_PORT = 71,
    ENV_DATA_PORT = 72,
    ENV_RESPONSE_PORT = 200,
    ENV_COMMAND_RESET = 0,
    ENV_COMMAND_INITIALIZE = 1,
    ENV_COMMAND_GET = 2,
    ENV_COMMAND_SET = 3,

    TIMER0_HIGH_PORT = 24,
    TIMER0_LOW_PORT = 25,
    TIMER1_HIGH_PORT = 26,
    TIMER1_LOW_PORT = 27,
    TIMER2_HIGH_PORT = 28,
    TIMER2_LOW_PORT = 29
};

extern int inp(unsigned port);
extern void outp(unsigned port, unsigned val);
extern int bdos(int func, int val);

static char console_buffer[CONSOLE_BUFFER_SIZE];

static char system_prompt[SYSTEM_PROMPT_LENGTH];
static char max_tokens_value[CONFIG_VALUE_LENGTH];
static char temperature_value[CONFIG_VALUE_LENGTH];
static char model_name[MODEL_NAME_LENGTH];
static int message_types[MESSAGE_HISTORY_LIMIT];
static int message_count;
static int response_truncated;

static char request_buffer[REQUEST_BUFFER_LENGTH];
static char response_buffer[ASSISTANT_RESPONSE_LENGTH];
static char *message_texts[MESSAGE_HISTORY_LIMIT];

static int wrap_column;
static int wrap_soft_break;
static int wrap_word_length;
static char wrap_word_buffer[WRAP_WORD_BUFFER_SIZE];

static int install_console_buffer(void);
static int console_write_char(int code);
static int console_read_char(void);
static int timer_port_for(int timer, int high);
static int timer_delay_milliseconds(int timer, unsigned ms);
static int console_move_cursor(int row, int col);
static int console_clear_screen(void);
static int console_set_color(int code);
static int console_reset_color(void);

static int env_initialize(void);
static int env_get_value(char *key, char *val, int maxlen);
static int env_set_value(char *key, char *val);

static int json_append_text(char *buf, int *pos, int maxlen, char *text);
static int json_append_escaped_text(char *buf, int *pos, int maxlen, char *text);
static int json_generate_chat_request(char *system_message, int *types, char **texts, int message_total,
                  char *outbuf, int outsize);

static int chat_initialize(void);
static int chat_load_system_prompt(void);
static int chat_run_session(void);
static int chat_add_message(int type, char *text);
static int chat_drop_oldest_message(void);
static int chat_show_history(void);
static int chat_clear_history(void);
static int chat_send_request(void);
static int chat_receive_response(char *buffer, int bufsize, int echo);
static int chat_print_wrapped(char *text);
static int chat_history_memory_used(void);
static int chat_largest_free_block(void);
static int chat_read_line(char *buf, int max);
static int chat_load_environment(void);
static int chat_store_numeric_config(char *value, char *destination, int allow_decimal);
static int chat_store_max_tokens(char *val);
static int chat_store_temperature(char *val);
static int chat_store_model(char *val);
static int chat_show_help(void);
static int chat_show_commands(void);
static int chat_matches_command(char *input, char *full_command, char *short_command);
static int wrap_reset(void);
static int wrap_flush_word(void);
static int wrap_write_char(int character_value);

static int install_console_buffer(void)
{
    return setvbuf(stdout, console_buffer, _IOFBF, CONSOLE_BUFFER_SIZE);
}

static int console_write_char(int code)
{
    return putchar(code & 0xff);
}

static int console_read_char(void)
{
    fflush(stdout);
    return bdos(1, 0) & 0xff;
}

static int timer_port_for(int timer, int high)
{
    if (timer == 0)
        return high ? TIMER0_HIGH_PORT : TIMER0_LOW_PORT;
    if (timer == 1)
        return high ? TIMER1_HIGH_PORT : TIMER1_LOW_PORT;
    if (timer == 2)
        return high ? TIMER2_HIGH_PORT : TIMER2_LOW_PORT;
    return -1;
}

static int timer_delay_milliseconds(int timer, unsigned ms)
{
    int hi_port;
    int lo_port;

    hi_port = timer_port_for(timer, 1);
    lo_port = timer_port_for(timer, 0);
    if (hi_port < 0 || lo_port < 0)
        return -1;

    outp((unsigned)hi_port, (ms >> 8) & 0xff);
    outp((unsigned)lo_port, ms & 0xff);
    while (inp((unsigned)lo_port) != 0)
        ;
    return 0;
}

static int console_move_cursor(int row, int col)
{
    printf("\033[%d;%dH", row, col);
    return 0;
}

static int console_clear_screen(void)
{
    printf("\033[2J\033[0m");
    console_move_cursor(1, 1);
    fflush(stdout);
    return 0;
}

static int console_set_color(int code)
{
    printf("\033[%dm", code);
    return 0;
}

static int console_reset_color(void)
{
    printf("\033[0m");
    return 0;
}

static int env_reset_request(void)
{
    outp(ENV_COMMAND_PORT, ENV_COMMAND_RESET);
    return 0;
}

static int env_read_status(void)
{
    int status_byte;

    status_byte = inp(ENV_COMMAND_PORT);
    if (status_byte > 127)
        status_byte -= 256;
    return status_byte;
}

static int env_send_string(char *value)
{
    while (*value != '\0') {
        outp(ENV_DATA_PORT, (unsigned)(*value & 0xff));
        value++;
    }
    outp(ENV_DATA_PORT, 0);
    return 0;
}

static int env_send_value(char *value)
{
    for (int index = 0; *value != '\0' && index < ENV_VALUE_LENGTH - 1; index++) {
        outp(ENV_DATA_PORT, (unsigned)(*value & 0xff));
        value++;
    }
    outp(ENV_DATA_PORT, 0);
    return 0;
}

static int env_read_string(char *destination, int destination_size)
{
    int index;
    int response_byte;

    index = 0;
    while (index < destination_size - 1) {
        response_byte = inp(ENV_RESPONSE_PORT);
        if (response_byte == 0)
            break;
        destination[index] = (char)(response_byte & 0x7f);
        index++;
    }
    destination[index] = '\0';
    return index;
}

static int env_initialize(void)
{
    env_reset_request();
    outp(ENV_COMMAND_PORT, ENV_COMMAND_INITIALIZE);
    return env_read_status();
}

static int env_get_value(char *key, char *val, int maxlen)
{
    int status_code;

    env_reset_request();
    env_send_string(key);
    outp(ENV_COMMAND_PORT, ENV_COMMAND_GET);
    status_code = env_read_status();
    if (status_code == ENV_STATUS_OK)
        env_read_string(val, maxlen);
    else
        val[0] = '\0';
    return status_code;
}

static int env_set_value(char *key, char *val)
{
    env_reset_request();
    env_send_string(key);
    env_send_value(val);
    outp(ENV_COMMAND_PORT, ENV_COMMAND_SET);
    return env_read_status();
}

static int json_append_text(char *buffer, int *position, int buffer_size, char *text)
{
    int write_position;

    write_position = *position;
    while (*text != '\0') {
        if (write_position >= buffer_size - 1) {
            buffer[write_position] = '\0';
            return -1;
        }
        buffer[write_position] = *text;
        write_position++;
        text++;
    }
    buffer[write_position] = '\0';
    *position = write_position;
    return 0;
}

static int json_append_escaped_text(char *buffer, int *position, int buffer_size, char *text)
{
    int write_position = *position;

    while (*text != '\0') {
        int character_value = *text++ & 0x7f;
        char escape_char = 0;

        if (character_value == '"' || character_value == '\\')
            escape_char = (char)character_value;
        else if (character_value == '\n')
            escape_char = 'n';
        else if (character_value == '\r')
            escape_char = 'r';
        else if (character_value == '\t')
            escape_char = 't';

        if (escape_char != 0) {
            if (write_position >= buffer_size - 2) {
                buffer[write_position] = '\0';
                return -1;
            }
            buffer[write_position++] = '\\';
            buffer[write_position++] = escape_char;
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

static int json_generate_chat_request(char *system_message, int *types, char **texts, int message_total,
                  char *outbuf, int outsize)
{
    int pos;
    int prev;
    char *message_text;

    pos = 0;
    if (outsize > 0)
        outbuf[0] = '\0';

    if (json_append_text(outbuf, &pos, outsize, "{\"model\":\"") < 0)
        return -1;
    if (json_append_text(outbuf, &pos, outsize, model_name) < 0)
        return -1;
    if (json_append_text(outbuf, &pos, outsize, "\",\"messages\":[") < 0)
        return -1;
    if (json_append_text(outbuf, &pos, outsize, "{\"role\":\"system\",\"content\":\"") < 0)
        return -1;
    if (json_append_escaped_text(outbuf, &pos, outsize, system_message) < 0)
        return -1;
    if (json_append_text(outbuf, &pos, outsize, "\"}") < 0)
        return -1;

    for (int index = 0; index < message_total; index++) {
        message_text = texts[index];
        if (message_text == NULL)
            message_text = "";

        prev = pos;
        if (json_append_text(outbuf, &pos, outsize, ",{\"role\":\"") < 0)
            return -1;

        if (json_append_text(outbuf, &pos, outsize,
                             types[index] == MESSAGE_TYPE_ASSISTANT ? "assistant" : "user") < 0) {
            pos = prev;
            outbuf[pos] = '\0';
            continue;
        }

        if (json_append_text(outbuf, &pos, outsize, "\",\"content\":\"") < 0) {
            pos = prev;
            outbuf[pos] = '\0';
            continue;
        }
        if (json_append_escaped_text(outbuf, &pos, outsize, message_text) < 0) {
            pos = prev;
            outbuf[pos] = '\0';
            continue;
        }
        if (json_append_text(outbuf, &pos, outsize, "\"}") < 0) {
            pos = prev;
            outbuf[pos] = '\0';
            continue;
        }
    }

    if (json_append_text(outbuf, &pos, outsize, "],\"max_tokens\":") < 0)
        return -1;
    if (json_append_text(outbuf, &pos, outsize, max_tokens_value) < 0)
        return -1;
    if (json_append_text(outbuf, &pos, outsize, ",\"temperature\":") < 0)
        return -1;
    if (json_append_text(outbuf, &pos, outsize, temperature_value) < 0)
        return -1;
    if (json_append_text(outbuf, &pos, outsize, ",\"stream\":true}") < 0)
        return -1;

    return pos;
}

static int chat_initialize(void)
{
    system_prompt[0] = '\0';
    strcpy(max_tokens_value, "1024");
    strcpy(temperature_value, "0.7");
    strcpy(model_name, "gemma3:1b");

    message_count = 0;
    response_truncated = 0;
    for (int index = 0; index < MESSAGE_HISTORY_LIMIT; index++) {
        message_types[index] = 0;
        message_texts[index] = NULL;
    }
    return 0;
}

static int chat_load_system_prompt(void)
{
    int character_value;
    int message_index;
    FILE *system_file;

    system_file = fopen("CHAT.SYS", "r");
    if (system_file == NULL)
        system_file = fopen("chat.sys", "r");
    if (system_file == NULL) {
        printf("Error: missing CHAT.SYS\n");
        fflush(stdout);
        return -1;
    }

    message_index = 0;
    while (message_index < SYSTEM_PROMPT_LENGTH - 1 && (character_value = getc(system_file)) != EOF) {
        if (character_value == 26)
            break;
        system_prompt[message_index++] = (char)(character_value & 0x7f);
    }
    system_prompt[message_index] = '\0';
    fclose(system_file);

    chat_load_environment();
    return 0;
}

static int chat_show_help(void)
{
    printf("CHATC89 - OpenAI / compatible chat client\n\n");
    printf("Usage: CHATC89 [-H]\n\n");
    printf("Setup may be done from the startup config menu\n");
    printf("or from CP/M using ESP32 ENV variables.\n\n");
    printf("Compatible example:\n");
    printf("  ENV CHAT_PROVIDER=compatible\n");
    printf("  ENV CHAT_ENDPOINT=http://host:11434/v1/chat/completions\n");
    printf("  ENV CHAT_MODEL=gemma3:1b\n");
    printf("  ENV CHAT_MAX_TOKENS=1024\n");
    printf("  ENV CHAT_TEMPERATURE=0.7\n\n");
    printf("OpenAI example:\n");
    printf("  ENV CHAT_PROVIDER=openai\n");
    printf("  ENV CHAT_OPENAI_KEY=your-api-key\n");
    printf("  ENV CHAT_MODEL=gpt-4o-mini\n\n");
    printf("Missing ENV values are seeded on first run.\n");
    fflush(stdout);
    return 0;
}

static int chat_load_environment(void)
{
    char val[ENV_VALUE_BUFFER_LENGTH];

    if (env_initialize() != 0)
        return 0;

    if (env_get_value("CHAT_MODEL", val, ENV_VALUE_BUFFER_LENGTH) == 0 && val[0] != '\0')
        chat_store_model(val);
    else
        env_set_value("CHAT_MODEL", model_name);

    if (env_get_value("CHAT_MAX_TOKENS", val, ENV_VALUE_BUFFER_LENGTH) == 0 && val[0] != '\0')
        chat_store_max_tokens(val);
    else
        env_set_value("CHAT_MAX_TOKENS", max_tokens_value);

    if (env_get_value("CHAT_TEMPERATURE", val, ENV_VALUE_BUFFER_LENGTH) == 0 && val[0] != '\0')
        chat_store_temperature(val);
    else
        env_set_value("CHAT_TEMPERATURE", temperature_value);

    return 0;
}

static int chat_store_numeric_config(char *value, char *destination, int allow_decimal)
{
    int write_length;
    int seen_decimal;
    char tmp[CONFIG_VALUE_LENGTH];

    write_length = 0;
    seen_decimal = 0;
    for (int index = 0; value[index] != '\0'; index++) {
        if (value[index] >= '0' && value[index] <= '9') {
            if (write_length < CONFIG_VALUE_LENGTH - 1)
                tmp[write_length++] = value[index];
        } else if (allow_decimal && value[index] == '.' && !seen_decimal) {
            if (write_length < CONFIG_VALUE_LENGTH - 1)
                tmp[write_length++] = value[index];
            seen_decimal = 1;
        } else {
            break;
        }
    }
    while (write_length > 0 && tmp[write_length - 1] == '.')
        write_length--;
    tmp[write_length] = '\0';
    if (write_length > 0)
        strcpy(destination, tmp);
    return 0;
}

static int chat_store_max_tokens(char *value)
{
    return chat_store_numeric_config(value, max_tokens_value, 0);
}

static int chat_store_temperature(char *value)
{
    return chat_store_numeric_config(value, temperature_value, 1);
}

static int chat_store_model(char *val)
{
    int write_length;
    int character_value;
    char tmp[MODEL_NAME_LENGTH];

    write_length = 0;
    for (int index = 0; val[index] != '\0'; index++) {
        character_value = val[index] & 0x7f;
        if ((character_value >= 'a' && character_value <= 'z') || (character_value >= 'A' && character_value <= 'Z') ||
            (character_value >= '0' && character_value <= '9') || character_value == '-' || character_value == '_' ||
            character_value == '.' || character_value == '/' || character_value == ':') {
            if (write_length < MODEL_NAME_LENGTH - 1)
                tmp[write_length++] = (char)character_value;
        } else {
            break;
        }
    }
    tmp[write_length] = '\0';
    if (write_length > 0)
        strcpy(model_name, tmp);
    return 0;
}

static int chat_show_commands(void)
{
    printf("Commands: /help (/?)  /history (/h)  /reset (/r)  /quit (/q)\n");
    return 0;
}

static int chat_matches_command(char *input, char *full_command, char *short_command)
{
    return strcmp(input, full_command) == 0 ||
           strcmp(input, full_command + 1) == 0 ||
           (short_command != NULL && strcmp(input, short_command) == 0);
}

static int chat_run_session(void)
{
    char input[USER_INPUT_LENGTH];

    console_clear_screen();
    printf("Altair 8800 Chat v%s\n", CHAT_VERSION);
    printf("=======================\n\n");
    printf("Model:       %s\n", model_name);
    printf("Temperature: %s\n", temperature_value);
    printf("Max tokens:  %s\n\n", max_tokens_value);
    printf("System (CHAT.SYS):\n%s\n\n", system_prompt);
    chat_show_commands();
    printf("\n");
    fflush(stdout);

    while (1) {
        console_set_color(ANSI_GREEN);
        printf("You: ");
        console_reset_color();
        fflush(stdout);

        chat_read_line(input, USER_INPUT_LENGTH);

        if (chat_matches_command(input, "/quit", "/q") ||
            chat_matches_command(input, "/exit", NULL))
            break;

        if (chat_matches_command(input, "/history", "/h")) {
            chat_show_history();
            continue;
        }
        if (chat_matches_command(input, "/reset", "/r")) {
            chat_clear_history();
            printf("\n");
            fflush(stdout);
            continue;
        }
        if (chat_matches_command(input, "/help", "/?") || strcmp(input, "?") == 0) {
            chat_show_commands();
            fflush(stdout);
            continue;
        }
        if (input[0] == '\0')
            continue;

        chat_add_message(MESSAGE_TYPE_USER, input);

        printf("\n");
        console_set_color(ANSI_CYAN);
        printf("Assistant:\n");
        console_reset_color();
        fflush(stdout);

        chat_send_request();

        printf("\n\n");
        fflush(stdout);
    }

    return 0;
}

static int chat_add_message(int type, char *text)
{
    int message_index;
    int text_length;
    char *message_copy;

    if (message_count >= MESSAGE_HISTORY_LIMIT)
        chat_drop_oldest_message();

    text_length = (int)strlen(text);
    message_copy = malloc((size_t)text_length + 1);
    if (message_copy == NULL) {
        printf("[out of memory adding message]\n");
        fflush(stdout);
        return -1;
    }
    strcpy(message_copy, text);

    message_index = message_count;
    message_types[message_index] = type;
    message_texts[message_index] = message_copy;
    message_count++;
    return 0;
}

static int chat_drop_oldest_message(void)
{
    if (message_count <= 0)
        return -1;
    if (message_texts[0] != NULL)
        free(message_texts[0]);

    for (int index = 0; index < message_count - 1; index++) {
        message_types[index] = message_types[index + 1];
        message_texts[index] = message_texts[index + 1];
    }
    message_count--;
    message_types[message_count] = 0;
    message_texts[message_count] = NULL;
    return 0;
}

static int chat_show_history(void)
{
    int history_memory;
    int free_memory;

    console_clear_screen();
    printf("=== Message History ===\n\n");

    console_set_color(ANSI_YELLOW);
    printf("System: ");
    chat_print_wrapped(system_prompt);
    printf("\n\n");
    console_reset_color();

    for (int index = 0; index < message_count; index++) {
        if (message_types[index] == MESSAGE_TYPE_USER) {
            console_set_color(ANSI_GREEN);
            printf("You: ");
        } else if (message_types[index] == MESSAGE_TYPE_ASSISTANT) {
            console_set_color(ANSI_CYAN);
            printf("Assistant: ");
        } else {
            printf("Unknown: ");
        }

        if (message_texts[index] != NULL)
            chat_print_wrapped(message_texts[index]);
        printf("\n");
        console_reset_color();
    }

    history_memory = chat_history_memory_used();
    free_memory = chat_largest_free_block();
    printf("Diag: msgs=%d  hist=%d  sys=%d  free~=%d\n",
            message_count, history_memory, (int)strlen(system_prompt) + 1, free_memory);
    printf("free~=largest alloc block, approximate\n\n");
    fflush(stdout);
    return 0;
}

static int chat_history_memory_used(void)
{
    int total;

    total = 0;
    for (int index = 0; index < message_count; index++) {
        if (message_texts[index] != NULL)
            total += (int)strlen(message_texts[index]) + 1;
    }
    return total;
}

static int chat_largest_free_block(void)
{
    int lower_bound;
    int upper_bound;
    int probe_size;
    char *probe_block;

    lower_bound = 0;
    upper_bound = 30000;
    while (lower_bound < upper_bound) {
        probe_size = (lower_bound + upper_bound + 1) / 2;
        probe_block = malloc((size_t)probe_size);
        if (probe_block != NULL) {
            free(probe_block);
            lower_bound = probe_size;
        } else {
            upper_bound = probe_size - 1;
        }
    }
    return lower_bound;
}

static int chat_clear_history(void)
{
    for (int index = 0; index < message_count; index++) {
        if (message_texts[index] != NULL)
            free(message_texts[index]);
        message_texts[index] = NULL;
        message_types[index] = 0;
    }
    message_count = 0;
    printf("\nMessage history cleared\n");
    fflush(stdout);
    return 0;
}

static int chat_send_request(void)
{
    int request_length;
    int response_length;
    int trimmed_messages;

    trimmed_messages = 0;
    request_length = json_generate_chat_request(system_prompt, message_types, message_texts, message_count, request_buffer, REQUEST_BUFFER_LENGTH);
    while (request_length < 0 && message_count > 0) {
        chat_drop_oldest_message();
        trimmed_messages++;
        request_length = json_generate_chat_request(system_prompt, message_types, message_texts, message_count, request_buffer, REQUEST_BUFFER_LENGTH);
    }

    if (request_length < 0) {
        printf("Error: JSON too large for buffer\n");
        fflush(stdout);
        return -1;
    }

    if (trimmed_messages > 0) {
        printf("[history trimmed: %d]\n", trimmed_messages);
        fflush(stdout);
    }

    outp(120, 1);
    outp(122, 1);

    for (int index = 0; index < request_length; index++)
        outp(121, (unsigned)(request_buffer[index] & 0xff));
    outp(121, 0);

    inp(120);

    response_length = chat_receive_response(response_buffer, ASSISTANT_RESPONSE_LENGTH, 1);
    if (response_length > 0) {
        chat_add_message(MESSAGE_TYPE_ASSISTANT, response_buffer);
        if (response_buffer[response_length - 1] != '\n')
            printf("\n");
        if (response_truncated)
            printf("[response truncated]\n");
    } else {
        printf("No response received\n");
    }
    fflush(stdout);
    return 0;
}

static int chat_receive_response(char *buffer, int bufsize, int echo)
{
    int status;
    int character_value;
    int pos;
    int timeout;
    int pending_newlines;
    int buffered_echo_count;

    pos = 0;
    timeout = 0;
    pending_newlines = 0;
    buffered_echo_count = 0;
    response_truncated = 0;
    buffer[0] = '\0';

    if (echo)
        wrap_reset();

    while (pos < bufsize - 1) {
        status = inp(123);
        if (status == OPENAI_STATUS_DATA_READY) {
            while (status == OPENAI_STATUS_DATA_READY && pos < bufsize - 1) {
                character_value = inp(124) & 0x7f;

                if (character_value == '\r') {
                    status = inp(123);
                    continue;
                }
                if (character_value == '\n') {
                    pending_newlines++;
                    status = inp(123);
                    continue;
                }

                if (pending_newlines > 0) {
                    if (pending_newlines == 1) {
                        if (pos > 0 && buffer[pos - 1] != ' ' && character_value != ' ' &&
                            pos < bufsize - 1) {
                            buffer[pos++] = ' ';
                            if (echo)
                                wrap_write_char(' ');
                        }
                    } else {
                        if (pos > 0 && buffer[pos - 1] != '\n' && pos < bufsize - 1) {
                            buffer[pos++] = '\n';
                            if (echo)
                                wrap_write_char('\n');
                        }
                        if (pos < bufsize - 1) {
                            buffer[pos++] = '\n';
                            if (echo)
                                wrap_write_char('\n');
                        }
                    }
                    pending_newlines = 0;
                }

                if (pos >= bufsize - 1) {
                    response_truncated = 1;
                    break;
                }

                buffer[pos++] = (char)character_value;
                if (echo) {
                    wrap_write_char(character_value);
                    buffered_echo_count++;
                    if (buffered_echo_count >= 96) {
                        fflush(stdout);
                        buffered_echo_count = 0;
                    }
                }
                status = inp(123);
            }
            if (status == OPENAI_STATUS_DATA_READY && pos >= bufsize - 1)
                response_truncated = 1;
            timeout = 0;
        } else if (status == OPENAI_STATUS_EOF) {
            break;
        } else {
            timeout++;
            if (timeout > 3000)
                break;
            if (echo && buffered_echo_count > 0) {
                fflush(stdout);
                buffered_echo_count = 0;
            }
            timer_delay_milliseconds(0, 10);
        }
    }

    if (echo) {
        wrap_flush_word();
        fflush(stdout);
    }
    buffer[pos] = '\0';
    return pos;
}

static int chat_read_line(char *buf, int max)
{
    int pos;
    int character_value;

    pos = 0;
    while (1) {
        character_value = console_read_char() & 0x7f;
        if (character_value == '\r' || character_value == '\n') {
            console_write_char('\n');
            break;
        }
        if (character_value == 8 || character_value == 127) {
            if (pos > 0) {
                pos--;
                console_write_char(' ');
                console_write_char(8);
            } else {
                console_write_char(' ');
            }
            fflush(stdout);
            continue;
        }
        if (character_value == 21) {
            while (pos > 0) {
                console_write_char(8);
                console_write_char(' ');
                console_write_char(8);
                pos--;
            }
            fflush(stdout);
            continue;
        }
        if (character_value >= ' ' && character_value < 127 && pos < max - 1)
            buf[pos++] = (char)character_value;
    }
    buf[pos] = '\0';
    fflush(stdout);
    return pos;
}

static int chat_print_wrapped(char *text)
{
    int character_value;

    wrap_reset();
    while ((character_value = *text & 0x7f) != 0) {
        wrap_write_char(character_value);
        text++;
    }
    wrap_flush_word();
    return 0;
}

static int wrap_reset(void)
{
    wrap_column = 0;
    wrap_soft_break = 0;
    wrap_word_length = 0;
    return 0;
}

static int wrap_flush_word(void)
{
    if (wrap_word_length == 0)
        return 0;

    if (wrap_column > 0 && wrap_word_length <= WRAP_LINE_WIDTH && wrap_column + wrap_word_length > WRAP_LINE_WIDTH) {
        console_write_char('\r');
        console_write_char('\n');
        wrap_column = 0;
        wrap_soft_break = 0;
    }

    for (int index = 0; index < wrap_word_length; index++) {
        console_write_char(wrap_word_buffer[index]);
        wrap_column++;
        if (wrap_column >= WRAP_LINE_WIDTH) {
            console_write_char('\r');
            console_write_char('\n');
            wrap_column = 0;
            wrap_soft_break = 1;
        } else {
            wrap_soft_break = 0;
        }
    }
    wrap_word_length = 0;
    return 0;
}

static int wrap_write_char(int character_value)
{
    character_value &= 0x7f;
    if (character_value == '\n') {
        wrap_flush_word();
        if (wrap_column == 0 && wrap_soft_break) {
            wrap_soft_break = 0;
            return 0;
        }
        console_write_char('\r');
        console_write_char('\n');
        wrap_column = 0;
        wrap_soft_break = 0;
        return 0;
    }
    if (character_value == '\r')
        return 0;
    if (character_value == ' ' || character_value == '\t') {
        wrap_flush_word();
        if (wrap_column == 0) {
            wrap_soft_break = 0;
            return 0;
        }
        if (wrap_column >= WRAP_LINE_WIDTH) {
            console_write_char('\r');
            console_write_char('\n');
            wrap_column = 0;
            wrap_soft_break = 0;
            return 0;
        }
        console_write_char(character_value);
        wrap_column++;
        if (wrap_column >= WRAP_LINE_WIDTH) {
            console_write_char('\r');
            console_write_char('\n');
            wrap_column = 0;
            wrap_soft_break = 1;
        } else {
            wrap_soft_break = 0;
        }
        return 0;
    }

    if (wrap_word_length >= WRAP_WORD_BUFFER_SIZE - 1)
        wrap_flush_word();
    wrap_word_buffer[wrap_word_length++] = (char)character_value;
    return 0;
}

int main(int argc, char *argv[])
{
    install_console_buffer();

    if (argc > 1) {
        if (strcmp(argv[1], "-H") == 0 || strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "/?") == 0)
            return chat_show_help();
    }

    console_clear_screen();
    if (chat_initialize() < 0) {
        printf("Error: failed to initialize\n");
        fflush(stdout);
        return 1;
    }
    if (chat_load_system_prompt() < 0)
        return 1;

    chat_run_session();
    fflush(stdout);
    return 0;
}
