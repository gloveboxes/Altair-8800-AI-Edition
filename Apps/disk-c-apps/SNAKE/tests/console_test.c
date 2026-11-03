#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

int bdos(int function, int argument);
int bios(int function, int argument);
#define main snake_main
#include "../snakec11.c"
#undef main

static unsigned char output[32768];
static unsigned output_used;
static int keys[16];
static unsigned key_count, key_position, input_calls;
static int arriving_keys[16];
static unsigned arriving_count, arrival_offset;
static unsigned timer_starts;
static int timer_running, quit_on_game_over;

int bios(int function, int argument)
{
    unsigned index;

    assert(function == 4);
    assert(argument >= 0 && argument <= 255);
    assert(output_used < sizeof output);
    output[output_used++] = (unsigned char)argument;
    if (output_used == arrival_offset) {
        assert(key_count + arriving_count <= sizeof keys / sizeof keys[0]);
        for (index = 0; index < arriving_count; ++index)
            keys[key_count++] = arriving_keys[index];
    }
    if (quit_on_game_over && game_state == GAME_OVER) {
        assert(console_buffer_used > 0);
        keys[key_count++] = 'q';
        quit_on_game_over = 0;
    }
    return 0;
}

int bdos(int function, int argument)
{
    /* Any normal-console output or input call fails this test. */
    assert(function == 6 && argument == 0xff);
    ++input_calls;
    return key_position < key_count ? keys[key_position++] : 0;
}

int svc_start_timer(int id, unsigned duration)
{
    assert(id == 2 && duration == 20);
    assert(console_buffer_used == 0);
    ++timer_starts;
    return 0;
}

int svc_timer_active(int id)
{
    assert(id == 2);
    return timer_running;
}

unsigned svc_random_u16(void)
{
    return 0;
}

static void reset_test(void)
{
    console_buffer_used = 0;
    console_foreground = console_background = 0;
    output_used = 0;
    key_count = key_position = input_calls = 0;
    arriving_count = arrival_offset = timer_starts = 0;
    timer_running = 1;
    quit_on_game_over = 0;
    memset(occupied, 0, sizeof occupied);
    initialize_game();
}

static void expect_output(const char *expected)
{
    assert(output_used == strlen(expected));
    assert(memcmp(output, expected, output_used) == 0);
    assert(console_buffer_used == 0);
}

static void test_buffer_boundaries(void)
{
    static const unsigned lengths[] = {
        0, 1, CONSOLE_BUFFER_SIZE - 1, CONSOLE_BUFFER_SIZE,
        CONSOLE_BUFFER_SIZE + 1, 2 * CONSOLE_BUFFER_SIZE + 17
    };
    static const char suffix[] = "\033[31m$65535";
    unsigned test, index, length;

    for (test = 0; test < sizeof lengths / sizeof lengths[0]; ++test) {
        reset_test();
        length = lengths[test];
        for (index = 0; index < length; ++index) {
            console_put_byte((uint8_t)index);
            assert(console_buffer_used <= CONSOLE_BUFFER_SIZE);
        }
        /* A full buffer is drained only when another byte needs space. */
        assert(output_used == (length == 0 ? 0 :
               ((length - 1) / CONSOLE_BUFFER_SIZE) * CONSOLE_BUFFER_SIZE));
        console_flush();
        assert(output_used == length && console_buffer_used == 0);
        for (index = 0; index < length; ++index)
            assert(output[index] == (uint8_t)index);
        console_flush();
        assert(output_used == length && input_calls == 0);
    }

    reset_test();
    for (index = 0; index < CONSOLE_BUFFER_SIZE - 1; ++index)
        console_put_byte('.');
    cputs("\033[31m$");
    write_unsigned(65535);
    assert(output_used == CONSOLE_BUFFER_SIZE);
    console_flush();
    assert(output_used == CONSOLE_BUFFER_SIZE - 1 + sizeof suffix - 1);
    for (index = 0; index < CONSOLE_BUFFER_SIZE - 1; ++index)
        assert(output[index] == '.');
    assert(memcmp(output + CONSOLE_BUFFER_SIZE - 1,
                  suffix, sizeof suffix - 1) == 0);
}

static void test_render_bytes(void)
{
    reset_test();
    clear_screen();
    move_cursor(5, 17);
    draw_tile(41);
    reset_graphics();
    cputs("$\r\n");
    write_unsigned(65535);
    write_spaces(2);
    write_unsigned(0);
    show_cursor();
    assert(output_used == 0);
    console_flush();
    expect_output("\033[0m\033[2J\033[H\033[?25l"
                  "\033[5;17H\033[41m  \033[0m$\r\n65535  0\033[?25h");
}

static void test_cursor_coordinates(void)
{
    unsigned row, column;
    char expected[16];

    reset_test();
    for (row = 1; row < 100; ++row) {
        for (column = 1; column < 100; ++column) {
            output_used = 0;
            move_cursor((uint8_t)row, (uint8_t)column);
            console_flush();
            snprintf(expected, sizeof expected, "\033[%u;%uH", row, column);
            expect_output(expected);
        }
    }
}

static void test_rendition_cache(void)
{
    reset_test();
    set_graphic_rendition(36);
    set_graphic_rendition(36);
    set_graphic_rendition(41);
    set_graphic_rendition(36);
    draw_tile(41);
    console_flush();
    expect_output("\033[36m\033[41m  ");

    output_used = 0;
    reset_graphics();
    reset_graphics();
    set_graphic_rendition(36);
    set_graphic_rendition(41);
    clear_screen();
    set_graphic_rendition(36);
    set_graphic_rendition(41);
    console_flush();
    expect_output("\033[0m\033[36m\033[41m"
                  "\033[0m\033[2J\033[H\033[?25l\033[36m\033[41m");
}

struct terminal_cell {
    unsigned char character, foreground, background;
};

struct terminal {
    struct terminal_cell cells[27][81];
    unsigned row, column, foreground, background;
};

static void apply_terminal_output(struct terminal *terminal)
{
    unsigned index = 0;

    while (index < output_used) {
        unsigned char byte = output[index++];
        if (byte == ESCAPE_KEY) {
            unsigned first = 0, second = 0;
            bool second_parameter = false;

            assert(index < output_used && output[index++] == '[');
            if (index < output_used && output[index] == '?')
                ++index;
            while (index < output_used) {
                byte = output[index++];
                if (byte >= '0' && byte <= '9') {
                    if (second_parameter)
                        second = second * 10 + byte - '0';
                    else
                        first = first * 10 + byte - '0';
                } else if (byte == ';') {
                    assert(!second_parameter);
                    second_parameter = true;
                } else {
                    break;
                }
            }
            if (byte == 'H') {
                terminal->row = first == 0 ? 1 : first;
                terminal->column = second == 0 ? 1 : second;
            } else if (byte == 'm') {
                if (first == 0)
                    terminal->foreground = terminal->background = 0;
                else if (first >= 30 && first <= 37)
                    terminal->foreground = first;
                else {
                    assert(first >= 40 && first <= 47);
                    terminal->background = first;
                }
            } else if (byte == 'J') {
                unsigned row, column;

                assert(first == 2);
                for (row = 1; row <= 26; ++row) {
                    for (column = 1; column <= 80; ++column) {
                        terminal->cells[row][column].character = ' ';
                        terminal->cells[row][column].foreground =
                            (unsigned char)terminal->foreground;
                        terminal->cells[row][column].background =
                            (unsigned char)terminal->background;
                    }
                }
            } else {
                assert((byte == 'h' || byte == 'l') && first == 25);
            }
        } else if (byte == '\r') {
            terminal->column = 1;
        } else if (byte == '\n') {
            ++terminal->row;
        } else {
            struct terminal_cell *cell;

            assert(terminal->row >= 1 && terminal->row <= 26);
            assert(terminal->column >= 1 && terminal->column <= 80);
            cell = &terminal->cells[terminal->row][terminal->column++];
            cell->character = byte;
            cell->foreground = (unsigned char)terminal->foreground;
            cell->background = (unsigned char)terminal->background;
        }
    }
}

static void expect_terminal_board(const struct terminal *terminal)
{
    unsigned row, column, offset;

    for (row = 0; row < BOARD_HEIGHT; ++row) {
        for (column = 0; column < BOARD_WIDTH; ++column) {
            unsigned background = 40;

            if (occupied[row][column])
                background = row == snake_rows[snake_head] &&
                             column == snake_columns[snake_head] ? 46 : 42;
            else if (food_present && row == food_row && column == food_column)
                background = 41;
            for (offset = 0; offset < 2; ++offset) {
                const struct terminal_cell *cell =
                    &terminal->cells[BOARD_SCREEN_ROW + row]
                                    [BOARD_SCREEN_COLUMN + 2 * column + offset];
                assert(cell->character == ' ' && cell->background == background);
            }
        }
    }
}

static void draw_test_screen(struct terminal *terminal)
{
    memset(terminal, 0, sizeof *terminal);
    clear_screen();
    draw_header();
    draw_board();
    draw_initial_snake();
    food_present = true;
    food_row = food_column = 0;
    draw_food();
    draw_status();
    reset_graphics();
    console_flush();
    apply_terminal_output(terminal);
    expect_terminal_board(terminal);
    output_used = 0;
}

static void test_direction_rendering(void)
{
    static const int row_steps[] = {-1, 1, 0, 0};
    static const int column_steps[] = {0, 0, -1, 1};
    struct terminal terminal, before;
    unsigned direction, index, step, row, column;

    for (direction = DIRECTION_UP; direction <= DIRECTION_RIGHT; ++direction) {
        reset_test();
        memset(occupied, 0, sizeof occupied);
        snake_direction = next_direction = (enum direction)direction;
        for (index = 0; index < snake_length; ++index) {
            snake_rows[index] = (uint8_t)(10 - row_steps[direction] * (int)index);
            snake_columns[index] =
                (uint8_t)(15 - column_steps[direction] * (int)index);
            occupied[snake_rows[index]][snake_columns[index]] = 1;
        }
        draw_test_screen(&terminal);
        before = terminal;
        for (step = 0; step < 3; ++step) {
            assert(advance_snake());
            console_flush();
            assert(output_used == (row_steps[direction] == 0 ? 37u : 45u));
            apply_terminal_output(&terminal);
            expect_terminal_board(&terminal);
            for (row = 1; row <= 26; ++row) {
                for (column = 1; column <= 80; ++column) {
                    if (row >= BOARD_SCREEN_ROW &&
                        row < BOARD_SCREEN_ROW + BOARD_HEIGHT &&
                        column >= BOARD_SCREEN_COLUMN &&
                        column < BOARD_SCREEN_COLUMN + 2 * BOARD_WIDTH)
                        continue;
                    assert(memcmp(&terminal.cells[row][column],
                                  &before.cells[row][column],
                                  sizeof(struct terminal_cell)) == 0);
                }
            }
            output_used = 0;
        }
    }
}

static void test_growth_and_tail_rendering(void)
{
    struct terminal terminal;
    unsigned index;
    static const char *status[] = {"Score: 50", "Length: 8", "Speed: 2"};

    reset_test();
    draw_test_screen(&terminal);
    for (index = 0; index < 5; ++index) {
        food_row = snake_rows[snake_head];
        food_column = (uint8_t)(snake_columns[snake_head] + 1);
        draw_food();
        console_flush();
        apply_terminal_output(&terminal);
        output_used = 0;
        assert(advance_snake());
        console_flush();
        apply_terminal_output(&terminal);
        expect_terminal_board(&terminal);
        assert(score == (index + 1) * 10);
        assert(snake_length == INITIAL_SNAKE_LENGTH + index + 1);
        output_used = 0;
    }
    assert(speed_level == 2);
    for (index = 0; index < 3; ++index) {
        unsigned column;

        for (column = 1; column < BORDER_SCREEN_COLUMN; ++column) {
            const struct terminal_cell *cell = &terminal.cells[6 + index][column];
            assert(cell->background == 0);
            if (column <= strlen(status[index]))
                assert(cell->character == status[index][column - 1]);
        }
    }

    reset_test();
    snake_length = 4;
    snake_rows[3] = 11;
    snake_columns[3] = 15;
    snake_rows[2] = 11;
    snake_columns[2] = 14;
    memset(occupied, 0, sizeof occupied);
    for (index = 0; index < snake_length; ++index)
        occupied[snake_rows[index]][snake_columns[index]] = 1;
    snake_direction = next_direction = DIRECTION_DOWN;
    draw_test_screen(&terminal);
    assert(advance_snake());
    console_flush();
    assert(output_used == 30);
    apply_terminal_output(&terminal);
    expect_terminal_board(&terminal);
    assert(game_state == GAME_PLAYING && snake_length == 4);
}

static void test_keys_during_drawing(void)
{
    static const int movement_keys[] = {
        CTRL_UP_KEY, CTRL_DOWN_KEY, CTRL_LEFT_KEY, CTRL_RIGHT_KEY
    };
    static const enum direction directions[] = {
        DIRECTION_UP, DIRECTION_DOWN, DIRECTION_LEFT, DIRECTION_RIGHT
    };
    unsigned test;

    for (test = 0; test < sizeof movement_keys / sizeof movement_keys[0]; ++test) {
        reset_test();
        snake_direction = next_direction = test < 2 ?
            DIRECTION_RIGHT : DIRECTION_UP;
        arriving_keys[0] = movement_keys[test];
        arriving_count = 1;
        arrival_offset = 100;
        draw_board();
        draw_status();
        console_flush();
        assert(output_used > arrival_offset);
        assert(key_count == 1 && key_position == 0 && input_calls == 0);
        process_input();
        assert(key_position == 1 && next_direction == directions[test]);
        assert(game_state == GAME_PLAYING);
    }

    /* Input also survives an automatic full-buffer drain, including 0xff. */
    reset_test();
    arriving_keys[0] = CTRL_LEFT_KEY;
    arriving_keys[1] = CTRL_RIGHT_KEY;
    arriving_count = 2;
    arrival_offset = CONSOLE_BUFFER_SIZE / 2;
    for (test = 0; test < CONSOLE_BUFFER_SIZE + 1; ++test)
        console_put_byte(0xff);
    console_flush();
    assert(output_used == CONSOLE_BUFFER_SIZE + 1);
    assert(key_position == 0 && input_calls == 0);
    assert(read_key() == CTRL_LEFT_KEY);
    assert(read_key() == CTRL_RIGHT_KEY);
    assert(read_key() == 0);
}

static void test_ansi_and_escape(void)
{
    reset_test();
    snake_direction = next_direction = DIRECTION_UP;
    arriving_keys[0] = ESCAPE_KEY;
    arriving_keys[1] = '[';
    arriving_keys[2] = 'C';
    arriving_count = 3;
    arrival_offset = 2;
    draw_snake_segment(10, 15, true);
    console_flush();
    process_input();
    process_input();
    process_input();
    assert(next_direction == DIRECTION_RIGHT);
    assert(escape_state == ESCAPE_STATE_NONE && game_state == GAME_PLAYING);

    assert(decode_key(ESCAPE_KEY) == KEY_ACTION_NONE);
    update_escape_timeout();
    update_escape_timeout();
    assert(game_state == GAME_PLAYING);
    update_escape_timeout();
    assert(game_state == GAME_QUIT);
}

static void test_movement_update(void)
{
    reset_test();
    assert(movement_delay() == 9);
    assert(advance_snake());
    assert(output_used == 0 && console_buffer_used > 0);
    console_flush();
    expect_output("\033[15;43H\033[40m  "
                  "\033[15;47H\033[42m  "
                  "\033[46m  ");
    assert(snake_length == INITIAL_SNAKE_LENGTH && score == 0);
    assert(snake_columns[snake_head] == BOARD_WIDTH / 2 + 1);
    speed_level = 6;
    assert(movement_delay() == 4);
    start_frame_timer();
    assert(frame_timer_active() && timer_starts == 1);
}

static void test_main_flushes(void)
{
    static const char ending[] =
        "\033[26;1H\033[?25hThanks for playing Snake!\r\n";

    reset_test();
    arriving_keys[0] = 'q';
    arriving_count = 1;
    arrival_offset = 100;
    assert(snake_main() == 0);
    assert(game_state == GAME_QUIT && key_position == 1);
    assert(timer_starts == 1 && console_buffer_used == 0);
    assert(output_used > sizeof ending - 1);
    assert(memcmp(output + output_used - (sizeof ending - 1),
                  ending, sizeof ending - 1) == 0);

    reset_test();
    timer_running = 0;
    quit_on_game_over = 1;
    assert(snake_main() == 0);
    assert(game_state == GAME_OVER && key_position == 1);
    assert(quit_on_game_over == 0 && console_buffer_used == 0);
    assert(snake_columns[snake_head] == BOARD_WIDTH - 1);
    assert(timer_starts == (BOARD_WIDTH / 2) * movement_delay() + 1);
    assert(output_used < sizeof output);
    output[output_used] = '\0';
    assert(strstr((const char *)output, "GAME OVER!") != NULL);
    assert(strstr((const char *)output, "Press ESC/Q to quit") != NULL);
    assert(memcmp(output + output_used - (sizeof ending - 1),
                  ending, sizeof ending - 1) == 0);
}

int main(void)
{
    test_buffer_boundaries();
    test_render_bytes();
    test_cursor_coordinates();
    test_rendition_cache();
    test_direction_rendering();
    test_growth_and_tail_rendering();
    test_keys_during_drawing();
    test_ansi_and_escape();
    test_movement_update();
    test_main_flushes();
    puts("Snake raw console tests PASS");
    return 0;
}
