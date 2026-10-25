#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

int bdos(int function, int argument);
int bios(int function, int argument);
#define main tetris_main
#include "../tetris_c11.c"
#undef main

static unsigned char output[32768];
static unsigned output_used;
static int keys[16];
static unsigned key_count, key_position, input_calls;
static int arriving_keys[16];
static unsigned arriving_count, arrival_offset;
static unsigned timer_starts, timer_polls, active_polls;

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
    assert(id == 2 && duration == 25);
    assert(console_buffer_used == 0);
    assert(++timer_starts < 20);
    timer_polls = 0;
    return 0;
}

int svc_timer_active(int id)
{
    assert(id == 2);
    return timer_polls++ < active_polls;
}

unsigned svc_random_u16(void)
{
    return 0;
}

static void reset_test(void)
{
    console_buffer_used = output_used = 0;
    key_count = key_position = input_calls = 0;
    arriving_count = arrival_offset = timer_starts = timer_polls = 0;
    active_polls = 1;
    initialize_game();
    active_piece = PIECE_T;
    active_rotation = 0;
    active_column = 3;
    active_row = 2;
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
    static const char suffix[] = "\033[31m$65535 2147483647";
    unsigned test, index, length;

    for (test = 0; test < sizeof lengths / sizeof lengths[0]; ++test) {
        reset_test();
        length = lengths[test];
        for (index = 0; index < length; ++index) {
            console_put_byte((uint8_t)index);
            assert(console_buffer_used <= CONSOLE_BUFFER_SIZE);
        }
        assert(output_used == (length == 0 ? 0 :
               ((length - 1) / CONSOLE_BUFFER_SIZE) * CONSOLE_BUFFER_SIZE));
        flush_console();
        assert(output_used == length && console_buffer_used == 0);
        for (index = 0; index < length; ++index)
            assert(output[index] == (uint8_t)index);
        flush_console();
        assert(output_used == length && input_calls == 0);
    }

    reset_test();
    for (index = 0; index < CONSOLE_BUFFER_SIZE - 1; ++index)
        console_put_byte('.');
    write_text("\033[31m$");
    write_unsigned(65535);
    write_text(" ");
    write_score(2147483647L);
    assert(output_used == CONSOLE_BUFFER_SIZE);
    flush_console();
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
    hide_cursor();
    move_cursor(5, 30);
    draw_tile(PIECE_L);
    reset_color();
    write_text("$\r\n");
    write_unsigned(65535);
    write_text(" ");
    write_score(2147483647L);
    write_text(" ");
    write_score(0);
    show_cursor();
    assert(output_used == 0);
    flush_console();
    expect_output("\033[0m\033[2J\033[H\033[?25l"
                  "\033[5;30H\033[103m  \033[0m$\r\n"
                  "65535 2147483647 0\033[?25h");
}

static void test_keys_during_drawing(void)
{
    unsigned index;

    reset_test();
    arriving_keys[0] = CTRL_LEFT_KEY;
    arriving_keys[1] = CTRL_RIGHT_KEY;
    arriving_count = 2;
    arrival_offset = 100;
    draw_border();
    draw_board();
    flush_console();
    assert(output_used > arrival_offset);
    assert(key_count == 2 && key_position == 0 && input_calls == 0);
    apply_action(decode_key(read_key()));
    assert(active_column == 2);
    apply_action(decode_key(read_key()));
    assert(active_column == 3 && game_state == GAME_RUNNING);
    flush_console();

    reset_test();
    arriving_keys[0] = CTRL_LEFT_KEY;
    arriving_keys[1] = CTRL_RIGHT_KEY;
    arriving_count = 2;
    arrival_offset = CONSOLE_BUFFER_SIZE / 2;
    for (index = 0; index < CONSOLE_BUFFER_SIZE + 1; ++index)
        console_put_byte(0xff);
    flush_console();
    assert(output_used == CONSOLE_BUFFER_SIZE + 1);
    assert(key_position == 0 && input_calls == 0);
    assert(read_key() == CTRL_LEFT_KEY);
    assert(read_key() == CTRL_RIGHT_KEY);
    assert(read_key() == 0);
}

static void test_controls_and_gravity(void)
{
    unsigned index;

    reset_test();
    arriving_keys[0] = ESCAPE_KEY;
    arriving_keys[1] = '[';
    arriving_keys[2] = 'C';
    arriving_count = 3;
    arrival_offset = 2;
    draw_active_piece();
    flush_console();
    for (index = 0; index < 3; ++index)
        apply_action(decode_key(read_key()));
    assert(active_column == 4 && escape_state == ESCAPE_STATE_NONE);
    apply_action(decode_key(CTRL_UP_KEY));
    assert(active_rotation == 1);
    apply_action(decode_key(CTRL_DOWN_KEY));
    assert(active_row == 3 && score == 1);
    assert(gravity_delay == 20);
    for (index = 0; index < 19; ++index)
        advance_gravity();
    assert(active_row == 3);
    advance_gravity();
    assert(active_row == 4 && gravity_tick == 0);
    level = 15;
    update_gravity_delay();
    assert(gravity_delay == 5);
    flush_console();
    start_frame_timer();
    assert(frame_timer_active() && timer_starts == 1);

    reset_test();
    active_piece = PIECE_O;
    active_row = 0;
    apply_action(decode_key(SPACE_KEY));
    assert(score == 34);
    assert(board[18][4] == PIECE_O && board[19][5] == PIECE_O);
    assert(active_row == -1 && game_state == GAME_RUNNING);

    reset_test();
    for (index = 0; index < BOARD_WIDTH; ++index)
        board[BOARD_HEIGHT - 1][index] = PIECE_J;
    assert(clear_full_lines() == 1);
    assert(lines_cleared == 1 && score == 40);
    for (index = 0; index < BOARD_WIDTH; ++index)
        assert(board[BOARD_HEIGHT - 1][index] == PIECE_NONE);
}

static void test_main_flushes_and_escape(void)
{
    static const char ending[] =
        "\033[24;1H\033[?25hThanks for playing Tetris!\r\n";
    unsigned test;

    for (test = 0; test < 2; ++test) {
        reset_test();
        arriving_keys[0] = test == 0 ? 'q' : ESCAPE_KEY;
        arriving_count = 1;
        arrival_offset = 100;
        assert(tetris_main() == 0);
        assert(game_state == GAME_QUIT && key_position == 1);
        assert(timer_starts == (test == 0 ? 1 : 3));
        assert(console_buffer_used == 0);
        assert(output_used > sizeof ending - 1 && output_used < sizeof output);
        assert(memcmp(output + output_used - (sizeof ending - 1),
                      ending, sizeof ending - 1) == 0);
        output[output_used] = '\0';
        assert(strstr((const char *)output, "GAME QUIT ") != NULL);
        assert(strstr((const char *)output, "Final Score: ") != NULL);
    }

    reset_test();
    game_state = GAME_OVER;
    draw_final_message();
    assert(output_used == 0 && console_buffer_used > 0);
    flush_console();
    output[output_used] = '\0';
    assert(strstr((const char *)output, "GAME OVER!") != NULL);
}

int main(void)
{
    test_buffer_boundaries();
    test_render_bytes();
    test_keys_during_drawing();
    test_controls_and_gravity();
    test_main_flushes_and_escape();
    puts("Tetris raw console tests PASS");
    return 0;
}
