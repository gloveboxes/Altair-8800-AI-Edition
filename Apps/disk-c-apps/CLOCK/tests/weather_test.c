#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char output[8192];
static size_t output_size;
static int diagnostics = 1;
static int status_result = 1;
static unsigned char status_value;
static int failed_field = -1;
static int field_reads;
static int text_result;
static int text_reads;

static int capture_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = vsnprintf(output + output_size, sizeof output - output_size, format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof output - output_size);
    output_size += length;
    return length;
}

static int capture_putchar(int byte)
{
    assert(output_size + 1 < sizeof output);
    output[output_size++] = byte;
    output[output_size] = '\0';
    return byte;
}

#define printf capture_printf
#define putchar capture_putchar
#define main clock_main
#include "../CLOCK.C"
#undef main
#undef putchar
#undef printf

int svc_error_output(int enabled)
{
    int previous = diagnostics;
    diagnostics = enabled != 0;
    return previous;
}

int svc_call_service(int group, const void *request, int length,
                     void *response, int capacity)
{
    assert(group == SERVICE_GROUP_WEATHER && length == 1 && capacity == 1);
    assert(*(const unsigned char *)request == 1 && diagnostics == 0);
    *(unsigned char *)response = status_value;
    return status_result;
}

int svc_query_text(int group, int operation, int field, char *response, int capacity)
{
    assert(group == SERVICE_GROUP_WEATHER && operation == 0 && diagnostics == 0);
    field_reads++;
    if (field == failed_field) {
        response[0] = '\0';
        return -20;
    }
    assert(capacity >= 2);
    strcpy(response, "x");
    return 1;
}

int svc_get_text(int group, int operation, char *response, int capacity)
{
    assert(group == SERVICE_GROUP_CLOCK && diagnostics == 0);
    (void)operation; (void)capacity;
    text_reads++;
    response[0] = '\0';
    return text_result;
}

int svc_start_timer(int id, unsigned duration)
{
    (void)id; (void)duration;
    return 0;
}

int svc_timer_active(int id) { (void)id; return 0; }
int getch(void) { return ESCAPE_KEY; }
int kbhit(void) { return 1; }

static void reset_output(void)
{
    output_size = 0;
    output[0] = '\0';
    field_reads = 0;
}

static void assert_unavailable(void)
{
    assert(strstr(output, "\033[22;31H\033[0;90mWeather unavailable\033[0m"));
    assert(strchr(output, '\r') == NULL && strchr(output, '\n') == NULL);
    assert(strstr(output, "Service I/O error") == NULL);
    assert(diagnostics == 1);
}

static void assert_waiting_border(void)
{
    char line[SCREEN_WIDTH];
    memset(line, 'x', sizeof line);
    line[0] = line[1] = line[SCREEN_WIDTH - 2] = line[SCREEN_WIDTH - 1] = '#';
    int row = 1;
    int col = 1;
    const char *cursor = output;
    while (*cursor) {
        if (*cursor == ESCAPE_KEY) {
            assert(*++cursor == '[');
            cursor++;
            char *end;
            int first = (int)strtol(cursor, &end, 10);
            cursor = end;
            int second = 0;
            if (*cursor == ';') {
                cursor++;
                second = (int)strtol(cursor, &end, 10);
                cursor = end;
            }
            if (*cursor == 'H') {
                row = first;
                col = second;
            } else if (*cursor == 'K') {
                assert(first == 0);
                if (row == CLOCK_ROW + 3)
                    memset(line + col - 1, ' ', SCREEN_WIDTH - col + 1);
            } else {
                assert(*cursor == 'm');
            }
            cursor++;
        } else {
            assert(col >= 1 && col <= SCREEN_WIDTH);
            if (row == CLOCK_ROW + 3)
                line[col - 1] = *cursor;
            col++;
            cursor++;
        }
    }
    assert(line[0] == '#' && line[1] == '#');
    assert(line[SCREEN_WIDTH - 2] == '#' && line[SCREEN_WIDTH - 1] == '#');
    const char *message = "Waiting for SNTP: ";
    assert(memcmp(line + CLOCK_COLUMN - 1, message, strlen(message)) == 0);
    for (int index = CLOCK_COLUMN - 1 + (int)strlen(message);
         index < SCREEN_WIDTH - 2; index++)
        assert(line[index] == ' ');
    assert(diagnostics == 1);
}

int main(void)
{
    setup();
    status_value = WS_ERR;
    weather_last = WS_ERR;
    const int time_errors[] = {-21, -20, 0};
    for (unsigned index = 0; index < sizeof time_errors / sizeof time_errors[0]; index++) {
        text_result = time_errors[index];
        reset_output();
        refresh_display();
        assert_waiting_border();
    }
    text_reads = 0;
    text_result = -21;
    assert(get_time() == -21);
    assert(get_date() == -21);
    show_uptime(UPTIME_ROW);
    assert(text_reads == 3 && diagnostics == 1);

    status_value = WS_OK;
    assert(weather_status_read() == WS_OK && diagnostics == 1);
    status_result = -20;
    reset_output();
    draw_weather(weather_status_read());
    assert_unavailable();
    assert(field_reads == 0);
    status_result = 1;
    status_value = 255;
    assert(weather_status_read() == WS_ERR && diagnostics == 1);

    for (int status = WS_NONE; status <= WS_ERR; status++) {
        reset_output();
        draw_weather(status);
        if (status == WS_NONE || status == WS_ERR) {
            assert_unavailable();
            assert(field_reads == 0);
        } else if (status == WS_FETCH) {
            assert(strstr(output, "Weather unavailable") == NULL && field_reads == 0);
        } else {
            assert(field_reads == 10 && strstr(output, "Now : x"));
            assert(strstr(output, "+3h : x") && diagnostics == 1);
        }
    }

    const int fields[] = {WF_CITY, WF_UNIT, WF_CMAIN, WF_CTEMP, WF_CFL,
                          WF_CHUM, WF_CWIND, WF_FMAIN, WF_FTEMP, WF_FFL};
    for (unsigned index = 0; index < sizeof fields / sizeof fields[0]; index++) {
        failed_field = fields[index];
        reset_output();
        draw_weather(WS_OK);
        assert_unavailable();
        assert(field_reads == (int)index + 1);
        assert(strstr(output, "Now :") == NULL);
    }
    puts("CLOCK weather display tests passed");
    return 0;
}
