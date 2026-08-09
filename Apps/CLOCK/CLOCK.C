#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
 * CLOCK.C - dcc C11 port of the BDS C CLOCK app.
 *
 * Reads ESP32 local wall clock through port 43, ticks every 50 ms,
 * and redraws HH:MM as chunky VT100 character blocks. Output goes
 * through stdio with a 2 KB fully buffered console buffer; only actual
 * VT100 update batches are flushed so idle timer polls do not drain it.
 */

#define TIMER_HIGH_PORT 28
#define TIMER_LOW_PORT 29
#define TIME_PORT 43
#define DATE_PORT 44
#define UPTIME_PORT 41
#define RESPONSE_PORT 200

#define WEATHER_FIELD_PORT 46
#define WEATHER_STATUS_PORT 47

enum weather_status {
    WS_UNKNOWN = -1,
    WS_NONE,
    WS_FETCH,
    WS_OK,
    WS_ERR
};

enum weather_field {
    WF_CITY,
    WF_CMAIN,
    WF_CDESC,
    WF_CTEMP,
    WF_CHUM,
    WF_CWIND,
    WF_FMAIN,
    WF_FDESC,
    WF_FTEMP,
    WF_FWHEN,
    WF_AGE,
    WF_UNIT,
    WF_ERR,
    WF_CFL,
    WF_FFL
};

#define ESCAPE_KEY 27
#define CTRL_C_KEY 3

#define CLOCK_ROW 10
#define CLOCK_COLUMN 14
#define DATE_ROW 18
#define WEATHER_ROW 22
#define UPTIME_ROW 2
#define UPTIME_DISPLAY_WIDTH 15

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 30
#define CONSOLE_BUFFER_SIZE 2048

extern int inp(unsigned port);
extern void outp(unsigned port, unsigned val);
extern int getch(void);
extern int kbhit(void);

static char console_buffer[CONSOLE_BUFFER_SIZE];

static int prev_hour_tens;
static int prev_hour_ones;
static int prev_min_tens;
static int prev_min_ones;

static char time_buffer[24];
static int disp_hour_tens;
static int disp_hour_ones;
static int disp_min_tens;
static int disp_min_ones;
static bool blink_phase;

static char date_buffer[40];
static char prev_date[40];

static char weather_buffer[64];
static enum weather_status weather_last;
static int weather_tick;

static const int digit_colors[5] = {101, 103, 107, 102, 106};

static char city[40];
static char unit[4];
static char current_main[40];
static char current_temp[12];
static char current_feels[12];
static char current_humidity[12];
static char current_wind[12];
static char forecast_main[40];
static char forecast_temp[12];
static char forecast_feels[12];

static const char *const digit_rows[11][7] = {
    {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "},
    {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "},
    {" ### ", "#   #", "    #", "  ## ", " #   ", "#    ", "#####"},
    {"#### ", "    #", "    #", " ### ", "    #", "    #", "#### "},
    {"#   #", "#   #", "#   #", "#####", "    #", "    #", "    #"},
    {"#####", "#    ", "#    ", "#### ", "    #", "    #", "#### "},
    {" ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### "},
    {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "},
    {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "},
    {" ### ", "#   #", "#   #", " ####", "    #", "    #", " ### "},
    {"     ", "  #  ", "  #  ", "     ", "  #  ", "  #  ", "     "}};

static void put_text(const char *text)
{
    while (*text != '\0')
    {
        putchar(*text);
        text++;
    }
}

static void put_number(int value)
{
    char digits[6];
    int count;

    if (value == 0)
    {
        putchar('0');
        return;
    }

    count = 0;
    while (value > 0 && count < 6)
    {
        digits[count] = (char)((value % 10) + '0');
        count++;
        value = value / 10;
    }

    while (count > 0)
    {
        count--;
        putchar(digits[count]);
    }
}

static void cursor_move(int row, int col)
{
    putchar(ESCAPE_KEY);
    putchar('[');
    put_number(row);
    putchar(';');
    put_number(col);
    putchar('H');
}

static void set_sgr(int color)
{
    putchar(ESCAPE_KEY);
    putchar('[');
    put_number(color);
    putchar('m');
}

static void reset_color(void)
{
    put_text("\033[0m");
}

static void hide_cursor(void)
{
    put_text("\033[?25l");
}

static void show_cursor(void)
{
    put_text("\033[?25h");
}

static void clear_screen(void)
{
    reset_color();
    put_text("\033[2J");
    cursor_move(1, 1);
}

static void timer_set(unsigned ms)
{
    unsigned hi_byte;
    unsigned lo_byte;

    hi_byte = (ms >> 8) & 0xFF;
    lo_byte = ms & 0xFF;
    outp(TIMER_HIGH_PORT, hi_byte);
    outp(TIMER_LOW_PORT, lo_byte);
}

static bool timer_expired(void)
{
    return inp(TIMER_LOW_PORT) == 0;
}

static int read_string(char *buffer, int max_len)
{
    int index;
    int ch;

    index = 0;
    ch = inp(RESPONSE_PORT);
    while (ch != 0 && index < max_len - 1)
    {
        buffer[index] = (char)ch;
        index++;
        ch = inp(RESPONSE_PORT);
    }
    buffer[index] = '\0';
    return index;
}

static int get_time(void)
{
    outp(TIME_PORT, 0);
    return read_string(time_buffer, (int)sizeof(time_buffer));
}

static int get_date(void)
{
    outp(DATE_PORT, 0);
    return read_string(date_buffer, (int)sizeof(date_buffer));
}

static void show_uptime(int row)
{
    char uptime_buffer[32];
    long uptime_seconds;
    int hours;
    int minutes;
    int seconds;
    int col;

    outp(UPTIME_PORT, 1);
    read_string(uptime_buffer, (int)sizeof(uptime_buffer));
    uptime_seconds = atol(uptime_buffer);
    hours = (int)(uptime_seconds / 3600L);
    minutes = (int)((uptime_seconds % 3600L) / 60L);
    seconds = (int)(uptime_seconds % 60L);

    col = SCREEN_WIDTH - 5 - UPTIME_DISPLAY_WIDTH;
    cursor_move(row, col);
    printf("\033[2;37mUptime %02d:%02d:%02d\033[0m",
           hours, minutes, seconds);
}

static void weather_read(enum weather_field field, char *buffer, int capacity)
{
    outp(WEATHER_FIELD_PORT, (unsigned)field);
    read_string(buffer, capacity);
}

static int centered_col(int width)
{
    int col;

    col = ((SCREEN_WIDTH - width) / 2) + 1;
    if (col < 3)
        col = 3;
    return col;
}

static void show_date(int row)
{
    int text_len;
    int col;

    text_len = (int)strlen(date_buffer);

    cursor_move(row, 3);
    for (int col_index = 3; col_index < SCREEN_WIDTH - 2; col_index++)
        putchar(' ');

    col = centered_col(text_len);

    cursor_move(row, col);
    printf("\033[1;93m%s\033[0m", date_buffer);
}

static void erase_weather_row(int row)
{
    cursor_move(row, 3);
    for (int col_index = 3; col_index < SCREEN_WIDTH - 2; col_index++)
        putchar(' ');
}

static void draw_weather(enum weather_status status)
{
    int len0;
    int len1;
    int len2;
    int max_width;
    int start_col;
    bool weather_disabled;

    for (int row_offset = 0; row_offset < 3; row_offset++)
        erase_weather_row(WEATHER_ROW + row_offset);

    if (status == WS_NONE || status == WS_FETCH)
        return;

    if (status == WS_ERR)
    {
        weather_read(WF_ERR, weather_buffer, sizeof weather_buffer);
        weather_disabled = strcmp(weather_buffer,
                                  "libcurl not available - weather disabled") == 0;

        if (weather_disabled)
            len0 = 19;
        else
            len0 = 9 + (int)strlen(weather_buffer);
        start_col = centered_col(len0);

        cursor_move(WEATHER_ROW, start_col);
        if (weather_disabled)
            printf("\033[0;90mWeather unavailable\033[0m");
        else
            printf("\033[1;92mWeather: \033[1;91m%s\033[0m", weather_buffer);
        return;
    }

    weather_read(WF_CITY, city, sizeof city);
    weather_read(WF_UNIT, unit, sizeof unit);
    weather_read(WF_CMAIN, current_main, sizeof current_main);
    weather_read(WF_CTEMP, current_temp, sizeof current_temp);
    weather_read(WF_CFL, current_feels, sizeof current_feels);
    weather_read(WF_CHUM, current_humidity, sizeof current_humidity);
    weather_read(WF_CWIND, current_wind, sizeof current_wind);
    weather_read(WF_FMAIN, forecast_main, sizeof forecast_main);
    weather_read(WF_FTEMP, forecast_temp, sizeof forecast_temp);
    weather_read(WF_FFL, forecast_feels, sizeof forecast_feels);

    len0 = 9 + (int)strlen(city);
    len1 = 29 + (int)strlen(current_main) + (int)strlen(current_temp) +
           ((int)strlen(unit) * 2) + (int)strlen(current_feels) +
           (int)strlen(current_humidity) + (int)strlen(current_wind);
    len2 = 17 + (int)strlen(forecast_main) + (int)strlen(forecast_temp) +
           ((int)strlen(unit) * 2) + (int)strlen(forecast_feels);
    max_width = len0;
    if (len1 > max_width)
        max_width = len1;
    if (len2 > max_width)
        max_width = len2;

    start_col = centered_col(max_width);

    cursor_move(WEATHER_ROW, start_col);
    printf("\033[1;92mWeather  \033[0;92m%s\033[0m", city);

    cursor_move(WEATHER_ROW + 1, start_col);
    printf("\033[0;92m  Now : %s  %s%s feels %s%s  %s%% RH wind %s\033[0m",
           current_main, current_temp, unit, current_feels, unit,
           current_humidity, current_wind);

    cursor_move(WEATHER_ROW + 2, start_col);
    printf("\033[0;92m  +3h : %s  %s%s feels %s%s\033[0m",
           forecast_main, forecast_temp, unit, forecast_feels, unit);

}

static void draw_border(void)
{
    set_sgr(44);

    cursor_move(1, 1);
    for (int col = 0; col < SCREEN_WIDTH; col++)
        putchar(' ');

    cursor_move(SCREEN_HEIGHT, 1);
    for (int col = 0; col < SCREEN_WIDTH; col++)
        putchar(' ');

    for (int row = 2; row < SCREEN_HEIGHT; row++)
    {
        cursor_move(row, 1);
        putchar(' ');
        putchar(' ');
        cursor_move(row, SCREEN_WIDTH - 1);
        putchar(' ');
        putchar(' ');
    }

    reset_color();
}

static void draw_glyph(const char *glyph, int color)
{
    bool cell_on;
    bool last_on;
    bool style_set;

    last_on = false;
    style_set = false;
    for (int glyph_col = 0; glyph_col < 5; glyph_col++)
    {
        cell_on = glyph[glyph_col] != ' ';
        if (!style_set || cell_on != last_on)
        {
            if (cell_on)
                set_sgr(color);
            else
                reset_color();
            last_on = cell_on;
            style_set = true;
        }
        putchar(' ');
        putchar(' ');
    }

    reset_color();
    putchar(' ');
}

static void draw_clock(void)
{
    for (int row = 0; row < 7; row++)
    {
        cursor_move(CLOCK_ROW + row, CLOCK_COLUMN);
        draw_glyph(digit_rows[disp_hour_tens][row], digit_colors[0]);
        draw_glyph(digit_rows[disp_hour_ones][row], digit_colors[1]);
        if (blink_phase)
            draw_glyph(digit_rows[10][0], digit_colors[2]);
        else
            draw_glyph(digit_rows[10][row], digit_colors[2]);
        draw_glyph(digit_rows[disp_min_tens][row], digit_colors[3]);
        draw_glyph(digit_rows[disp_min_ones][row], digit_colors[4]);
    }
}

static void setup(void)
{
    prev_hour_tens = -1;
    prev_hour_ones = -1;
    prev_min_tens = -1;
    prev_min_ones = -1;
    blink_phase = false;
    weather_last = WS_UNKNOWN;
    weather_tick = 0;
    date_buffer[0] = '\0';
    prev_date[0] = '\0';

}

static int help(void)
{
    printf("CLOCK.C89 - Altair local time and weather display\n\n");
    printf("Usage: CLOCK [-H]\n\n");
    printf("Setup may be done from the startup config menu\n");
    printf("with a serial terminal connected, or from CP/M\n");
    printf("using ESP32 ENV variables.\n\n");
    printf("UTC offset examples:\n");
    printf("     ENV UTC_OFFSET=10.0\n");
    printf("     ENV UTC_OFFSET=8.5\n");
    printf("     ENV UTC_OFFSET=-8.5\n\n");
    printf("Restart the ESP32/emulator after changing UTC_OFFSET.\n");
    printf("The offset is read once at startup and cached by firmware.\n");
    return 0;
}

static int install_console_buffer(void)
{
    return setvbuf(stdout, console_buffer, _IOFBF, CONSOLE_BUFFER_SIZE);
}

static void refresh_display(void)
{
    bool screen_dirty = false;
    int time_len = get_time();

    if (time_len >= 19 && time_buffer[10] == 'T')
    {
        disp_hour_tens = time_buffer[11] - '0';
        disp_hour_ones = time_buffer[12] - '0';
        disp_min_tens = time_buffer[14] - '0';
        disp_min_ones = time_buffer[15] - '0';

        int seconds = (time_buffer[17] - '0') * 10 +
                      (time_buffer[18] - '0');
        bool changed_hour = (disp_hour_tens != prev_hour_tens) ||
                            (disp_hour_ones != prev_hour_ones);
        bool changed_minute = (disp_min_tens != prev_min_tens) ||
                              (disp_min_ones != prev_min_ones);
        bool new_blink = (seconds & 1) != 0;
        bool changed_second = new_blink != blink_phase;
        blink_phase = new_blink;

        if (changed_hour || changed_minute || changed_second)
        {
            draw_clock();
            prev_hour_tens = disp_hour_tens;
            prev_hour_ones = disp_hour_ones;
            prev_min_tens = disp_min_tens;
            prev_min_ones = disp_min_ones;
            screen_dirty = true;
        }

        if (changed_second || changed_minute || changed_hour)
        {
            show_uptime(UPTIME_ROW);
            screen_dirty = true;
        }

        if (changed_minute || changed_hour || prev_date[0] == '\0')
        {
            get_date();
            if (strcmp(date_buffer, prev_date) != 0)
            {
                show_date(DATE_ROW);
                strcpy(prev_date, date_buffer);
                screen_dirty = true;
            }
        }
    }
    else
    {
        cursor_move(CLOCK_ROW + 3, CLOCK_COLUMN);
        printf("\033[1;91mWaiting for SNTP: \033[0m\033[K");
        printf("\033[1;97m%s\033[0m", time_buffer);
        screen_dirty = true;
    }

    enum weather_status weather_now =
        (enum weather_status)inp(WEATHER_STATUS_PORT);
    if (weather_now == WS_OK)
        weather_tick++;
    else
        weather_tick = 0;

    if (weather_now != weather_last ||
        (weather_now == WS_OK && weather_tick >= 6000))
    {
        draw_weather(weather_now);
        weather_last = weather_now;
        weather_tick = 0;
        screen_dirty = true;
    }

    if (screen_dirty)
        fflush(stdout);
    timer_set(50);
}

int main(int argc, char *argv[])
{
    if (install_console_buffer() != 0)
        return EXIT_FAILURE;

    if (argc > 1)
    {
        if (strcmp(argv[1], "-H") == 0 || strcmp(argv[1], "-h") == 0 ||
            strcmp(argv[1], "/?") == 0)
            return help();
    }

    setup();
    clear_screen();
    hide_cursor();
    draw_border();

    show_uptime(UPTIME_ROW);
    draw_weather(WS_NONE);
    fflush(stdout);

    timer_set(50);

    for (;;)
    {
        if (timer_expired())
            refresh_display();

        if (kbhit())
        {
            int key = getch();
            if (key == ESCAPE_KEY || key == CTRL_C_KEY)
                break;
        }
    }

    reset_color();
    clear_screen();
    show_cursor();
    fflush(stdout);
    return EXIT_SUCCESS;
}
