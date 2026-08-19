/*
 * Snake for dcc C11, CP/M 2.2, and the Altair 8800 emulator.
 *
 * The snake is stored as a circular deque and an occupancy grid provides
 * constant-time collision checks. Console output is fully buffered and
 * flushed once per visible update; no printf formatter is linked.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TIMER_HIGH_PORT 28
#define TIMER_LOW_PORT 29
#define RANDOM_COMMAND_PORT 45
#define RANDOM_DATA_PORT 200
#define FRAME_MILLISECONDS 20

#define BOARD_WIDTH 30
#define BOARD_HEIGHT 20
#define BOARD_SCREEN_ROW 5
#define BOARD_SCREEN_COLUMN 17
#define BORDER_SCREEN_ROW 4
#define BORDER_SCREEN_COLUMN 15

#define MAX_SNAKE_LENGTH 200
#define INITIAL_SNAKE_LENGTH 3
#define RANDOM_PLACEMENT_ATTEMPTS 200
#define CONSOLE_BUFFER_SIZE 4096

#define ESCAPE_KEY 27
#define CTRL_C_KEY 3
#define CTRL_UP_KEY 5
#define CTRL_DOWN_KEY 24
#define CTRL_LEFT_KEY 19
#define CTRL_RIGHT_KEY 4

enum game_state {
    GAME_PLAYING,
    GAME_OVER,
    GAME_WON,
    GAME_QUIT
};

enum direction {
    DIRECTION_UP,
    DIRECTION_DOWN,
    DIRECTION_LEFT,
    DIRECTION_RIGHT
};

enum key_action {
    KEY_ACTION_NONE,
    KEY_ACTION_QUIT,
    KEY_ACTION_UP,
    KEY_ACTION_DOWN,
    KEY_ACTION_LEFT,
    KEY_ACTION_RIGHT
};

enum escape_state {
    ESCAPE_STATE_NONE,
    ESCAPE_STATE_SEEN,
    ESCAPE_STATE_BRACKET_SEEN
};

static char console_buffer[CONSOLE_BUFFER_SIZE];
static uint8_t snake_rows[MAX_SNAKE_LENGTH];
static uint8_t snake_columns[MAX_SNAKE_LENGTH];
static uint8_t occupied[BOARD_HEIGHT][BOARD_WIDTH];

static uint8_t snake_head;
static uint8_t snake_length;
static enum direction snake_direction;
static enum direction next_direction;

static uint8_t food_row;
static uint8_t food_column;
static bool food_present;

static enum game_state game_state;
static enum escape_state escape_state;
static uint8_t escape_wait_ticks;
static uint8_t movement_ticks;
static uint8_t speed_level;
static unsigned score;

static void cputs(const char *text)
{
    while (*text != '\0')
        putchar((unsigned char)*text++);
}

static void write_unsigned(unsigned value)
{
    char digits[5];
    uint8_t count = 0;

    if (value == 0) {
        putchar('0');
        return;
    }
    while (value != 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count != 0)
        putchar(digits[--count]);
}

static void write_spaces(uint8_t count)
{
    while (count-- != 0)
        putchar(' ');
}

static void move_cursor(uint8_t row, uint8_t column)
{
    cputs("\033[");
    write_unsigned(row);
    putchar(';');
    write_unsigned(column);
    putchar('H');
}

static void set_graphic_rendition(uint8_t code)
{
    cputs("\033[");
    if (code >= 40) {
        putchar('4');
        code -= 40;
    } else {
        putchar('3');
        code -= 30;
    }
    putchar((char)('0' + code));
    putchar('m');
}

static void reset_graphics(void)
{
    cputs("\033[0m");
}

static void clear_screen(void)
{
    cputs("\033[0m\033[2J\033[H\033[?25l");
}

static void show_cursor(void)
{
    cputs("\033[?25h");
}

static void draw_tile(uint8_t background)
{
    set_graphic_rendition(background);
    cputs("  ");
}

static uint8_t cabinet_background(uint8_t row, uint8_t column)
{
    return (((row >> 1) + (column >> 1)) & 1) != 0 ? 46 : 45;
}

static void draw_board(void)
{
    uint8_t row;
    uint8_t column;

    for (row = 0; row < BOARD_HEIGHT + 2; ++row) {
        move_cursor((uint8_t)(BORDER_SCREEN_ROW + row), BORDER_SCREEN_COLUMN);
        if (row == 0 || row == BOARD_HEIGHT + 1) {
            for (column = 0; column < BOARD_WIDTH + 2; column += 2) {
                set_graphic_rendition(cabinet_background(row, column));
                cputs("    ");
            }
        } else {
            draw_tile(cabinet_background(row, 0));
            set_graphic_rendition(40);
            write_spaces(BOARD_WIDTH * 2);
            draw_tile(cabinet_background(row, BOARD_WIDTH + 1));
        }
    }
    reset_graphics();
}

static void draw_header(void)
{
    move_cursor(1, 1);
    set_graphic_rendition(36);
    cputs("SNAKE");
    reset_graphics();
    cputs(" for ");
    set_graphic_rendition(33);
    cputs("Altair 8800");
    reset_graphics();
    cputs(" C11");

    move_cursor(2, 1);
    set_graphic_rendition(37);
    cputs("ARROWS Move  ESC/Q Quit  Eat red blocks, avoid the cabinet and yourself");
    reset_graphics();

    move_cursor(5, 1);
    set_graphic_rendition(35);
    cputs("STATUS");
    reset_graphics();
}

static void draw_status(void)
{
    move_cursor(6, 1);
    set_graphic_rendition(36);
    cputs("Score: ");
    set_graphic_rendition(33);
    write_unsigned(score);
    reset_graphics();
    cputs("  ");

    move_cursor(7, 1);
    set_graphic_rendition(36);
    cputs("Length: ");
    set_graphic_rendition(33);
    write_unsigned(snake_length);
    reset_graphics();
    cputs("   ");

    move_cursor(8, 1);
    set_graphic_rendition(36);
    cputs("Speed: ");
    set_graphic_rendition(33);
    write_unsigned(speed_level);
    reset_graphics();
    cputs("     ");
}

static void draw_cell(uint8_t row, uint8_t column, uint8_t background)
{
    move_cursor((uint8_t)(BOARD_SCREEN_ROW + row),
                (uint8_t)(BOARD_SCREEN_COLUMN + column * 2));
    draw_tile(background);
}

static void draw_snake_segment(uint8_t row, uint8_t column, bool is_head)
{
    draw_cell(row, column, is_head ? 46 : 42);
}

static void erase_cell(uint8_t row, uint8_t column)
{
    draw_cell(row, column, 40);
}

static void draw_food(void)
{
    draw_cell(food_row, food_column, 41);
}

static void start_frame_timer(void)
{
    outp(TIMER_HIGH_PORT, FRAME_MILLISECONDS >> 8);
    outp(TIMER_LOW_PORT, FRAME_MILLISECONDS & 0xff);
}

static bool frame_timer_active(void)
{
    return inp(TIMER_LOW_PORT) != 0;
}

static uint16_t random_word(void)
{
    uint16_t value;

    outp(RANDOM_COMMAND_PORT, 1);
    value = (uint8_t)inp(RANDOM_DATA_PORT);
    value |= (uint16_t)((uint8_t)inp(RANDOM_DATA_PORT)) << 8;
    return value;
}

static int read_key(void)
{
    return bdos(6, 0xff) & 0xff;
}

static bool place_food(void)
{
    uint8_t attempts;
    uint8_t row;
    uint8_t column;
    uint16_t cell;

    for (attempts = 0; attempts < RANDOM_PLACEMENT_ATTEMPTS; ++attempts) {
        cell = random_word() % (BOARD_WIDTH * BOARD_HEIGHT);
        row = (uint8_t)(cell / BOARD_WIDTH);
        column = (uint8_t)(cell % BOARD_WIDTH);
        if (occupied[row][column] == 0) {
            food_row = row;
            food_column = column;
            food_present = true;
            draw_food();
            return true;
        }
    }

    for (row = 0; row < BOARD_HEIGHT; ++row) {
        for (column = 0; column < BOARD_WIDTH; ++column) {
            if (occupied[row][column] == 0) {
                food_row = row;
                food_column = column;
                food_present = true;
                draw_food();
                return true;
            }
        }
    }
    return false;
}

static uint8_t tail_index(void)
{
    unsigned index = snake_head + snake_length - 1;

    if (index >= MAX_SNAKE_LENGTH)
        index -= MAX_SNAKE_LENGTH;
    return (uint8_t)index;
}

static bool directions_are_opposite(enum direction first,
                                    enum direction second)
{
    return (first == DIRECTION_UP && second == DIRECTION_DOWN) ||
           (first == DIRECTION_DOWN && second == DIRECTION_UP) ||
           (first == DIRECTION_LEFT && second == DIRECTION_RIGHT) ||
           (first == DIRECTION_RIGHT && second == DIRECTION_LEFT);
}

static enum key_action decode_key(int character)
{
    if (escape_state == ESCAPE_STATE_BRACKET_SEEN) {
        escape_state = ESCAPE_STATE_NONE;
        if (character == 'A') return KEY_ACTION_UP;
        if (character == 'B') return KEY_ACTION_DOWN;
        if (character == 'C') return KEY_ACTION_RIGHT;
        if (character == 'D') return KEY_ACTION_LEFT;
        return KEY_ACTION_NONE;
    }

    if (escape_state == ESCAPE_STATE_SEEN) {
        escape_state = ESCAPE_STATE_NONE;
        if (character == '[') {
            escape_state = ESCAPE_STATE_BRACKET_SEEN;
            escape_wait_ticks = 0;
            return KEY_ACTION_NONE;
        }
        return KEY_ACTION_QUIT;
    }

    if (character == ESCAPE_KEY) {
        escape_state = ESCAPE_STATE_SEEN;
        escape_wait_ticks = 0;
        return KEY_ACTION_NONE;
    }
    if (character == CTRL_C_KEY || character == 'q' || character == 'Q')
        return KEY_ACTION_QUIT;
    if (character == CTRL_UP_KEY) return KEY_ACTION_UP;
    if (character == CTRL_DOWN_KEY) return KEY_ACTION_DOWN;
    if (character == CTRL_LEFT_KEY) return KEY_ACTION_LEFT;
    if (character == CTRL_RIGHT_KEY) return KEY_ACTION_RIGHT;
    return KEY_ACTION_NONE;
}

static void request_direction(enum direction direction)
{
    if (!directions_are_opposite(direction, snake_direction) &&
        !directions_are_opposite(direction, next_direction))
        next_direction = direction;
}

static void process_input(void)
{
    int character = read_key();
    enum key_action action;

    if (character == 0)
        return;
    action = decode_key(character);

    switch (action) {
    case KEY_ACTION_QUIT:
        game_state = GAME_QUIT;
        break;
    case KEY_ACTION_UP:
        request_direction(DIRECTION_UP);
        break;
    case KEY_ACTION_DOWN:
        request_direction(DIRECTION_DOWN);
        break;
    case KEY_ACTION_LEFT:
        request_direction(DIRECTION_LEFT);
        break;
    case KEY_ACTION_RIGHT:
        request_direction(DIRECTION_RIGHT);
        break;
    default:
        break;
    }
}

static void update_escape_timeout(void)
{
    if (escape_state == ESCAPE_STATE_NONE)
        return;
    if (++escape_wait_ticks <= 2)
        return;

    if (escape_state == ESCAPE_STATE_SEEN)
        game_state = GAME_QUIT;
    escape_state = ESCAPE_STATE_NONE;
}

static uint8_t movement_delay(void)
{
    if (speed_level >= 6)
        return 4;
    return (uint8_t)(10 - speed_level);
}

static bool advance_snake(void)
{
    int next_row;
    int next_column;
    uint8_t old_head_row;
    uint8_t old_head_column;
    uint8_t old_tail_index;
    uint8_t old_tail_row;
    uint8_t old_tail_column;
    uint8_t new_head_index;
    bool ate;
    bool grow;

    snake_direction = next_direction;
    old_head_row = snake_rows[snake_head];
    old_head_column = snake_columns[snake_head];
    next_row = old_head_row;
    next_column = old_head_column;

    if (snake_direction == DIRECTION_UP) --next_row;
    if (snake_direction == DIRECTION_DOWN) ++next_row;
    if (snake_direction == DIRECTION_LEFT) --next_column;
    if (snake_direction == DIRECTION_RIGHT) ++next_column;

    if (next_row < 0 || next_row >= BOARD_HEIGHT ||
        next_column < 0 || next_column >= BOARD_WIDTH) {
        game_state = GAME_OVER;
        return false;
    }

    ate = food_present && next_row == food_row && next_column == food_column;
    grow = ate && snake_length < MAX_SNAKE_LENGTH;
    old_tail_index = tail_index();
    old_tail_row = snake_rows[old_tail_index];
    old_tail_column = snake_columns[old_tail_index];

    if (occupied[next_row][next_column] != 0 &&
        (grow || next_row != old_tail_row || next_column != old_tail_column)) {
        game_state = GAME_OVER;
        return false;
    }

    if (ate) {
        food_present = false;
        score += 10;
        if (score % 50 == 0 && speed_level < 10)
            ++speed_level;
    }

    if (!grow)
        occupied[old_tail_row][old_tail_column] = 0;
    else
        ++snake_length;

    new_head_index = snake_head == 0 ? MAX_SNAKE_LENGTH - 1 : snake_head - 1;
    snake_head = new_head_index;
    snake_rows[snake_head] = (uint8_t)next_row;
    snake_columns[snake_head] = (uint8_t)next_column;
    occupied[next_row][next_column] = 1;

    if (!grow)
        erase_cell(old_tail_row, old_tail_column);
    draw_snake_segment(old_head_row, old_head_column, false);
    draw_snake_segment((uint8_t)next_row, (uint8_t)next_column, true);

    if (ate) {
        if (!place_food())
            game_state = GAME_WON;
        draw_status();
    }
    reset_graphics();
    return true;
}

static void initialize_game(void)
{
    uint8_t index;

    snake_head = 0;
    snake_length = INITIAL_SNAKE_LENGTH;
    snake_direction = DIRECTION_RIGHT;
    next_direction = DIRECTION_RIGHT;
    food_present = false;
    game_state = GAME_PLAYING;
    escape_state = ESCAPE_STATE_NONE;
    escape_wait_ticks = 0;
    movement_ticks = 0;
    speed_level = 1;
    score = 0;

    for (index = 0; index < snake_length; ++index) {
        snake_rows[index] = BOARD_HEIGHT / 2;
        snake_columns[index] = (uint8_t)(BOARD_WIDTH / 2 - index);
        occupied[snake_rows[index]][snake_columns[index]] = 1;
    }
}

static void draw_initial_snake(void)
{
    uint8_t index;

    for (index = 0; index < snake_length; ++index)
        draw_snake_segment(snake_rows[index], snake_columns[index], index == 0);
}

static void draw_final_message(void)
{
    reset_graphics();
    move_cursor(15, 34);
    set_graphic_rendition(game_state == GAME_WON ? 36 : 35);
    cputs(game_state == GAME_WON ? "YOU WIN!  " : "GAME OVER!");
    reset_graphics();

    move_cursor(16, 31);
    set_graphic_rendition(33);
    cputs("Final Score: ");
    write_unsigned(score);
    cputs("  ");
    reset_graphics();

    move_cursor(17, 31);
    set_graphic_rendition(33);
    cputs("Final Length: ");
    write_unsigned(snake_length);
    cputs("  ");
    reset_graphics();

    move_cursor(18, 31);
    cputs("Press ESC/Q to quit");
}

static void wait_for_quit(void)
{
    int character;

    for (;;) {
        character = read_key();
        if (character == ESCAPE_KEY || character == CTRL_C_KEY ||
            character == 'q' || character == 'Q')
            return;
    }
}

int main(void)
{
    setvbuf(stdout, console_buffer, _IOFBF, sizeof(console_buffer));
    initialize_game();

    clear_screen();
    draw_header();
    draw_board();
    draw_initial_snake();
    if (!place_food())
        game_state = GAME_WON;
    draw_status();
    reset_graphics();
    fflush(stdout);

    start_frame_timer();
    while (game_state == GAME_PLAYING) {
        while (frame_timer_active() && game_state == GAME_PLAYING)
            process_input();
        if (game_state != GAME_PLAYING)
            break;

        update_escape_timeout();
        if (game_state != GAME_PLAYING)
            break;

        if (++movement_ticks >= movement_delay()) {
            movement_ticks = 0;
            if (advance_snake())
                fflush(stdout);
        }
        start_frame_timer();
    }

    if (game_state == GAME_OVER || game_state == GAME_WON) {
        draw_final_message();
        fflush(stdout);
        wait_for_quit();
    }

    reset_graphics();
    move_cursor(26, 1);
    show_cursor();
    cputs("Thanks for playing Snake!\r\n");
    fflush(stdout);
    return 0;
}