#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

int bdos(int function, int argument);
int bios(int function, int argument);
#define main breakout_main
#include "../breakc11.c"
#undef main

static unsigned char output[32768];
static unsigned output_used;
static int keys[16];
static unsigned key_count, key_position, input_calls;
static int arriving_keys[16];
static unsigned arriving_count, arrival_offset;
static unsigned timer_starts;
static int finish_game;
static int speaker_present = 1, sound_calls, sound_stops, last_preset, last_priority;
int snd_init(void) { return speaker_present; }
int snd_stop(void) { ++sound_stops; return 1; }
int snd_fx(int priority, int effect, ...)
{
    va_list args;
    assert(effect == SND_FX_PRESET);
    va_start(args, effect);
    last_preset = va_arg(args, int);
    va_end(args);
    last_priority = priority;
    ++sound_calls;
    return 1;
}

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
    assert(id == 1 && duration == ball_delay_ms);
    assert(duration == 80 || duration == 70 || duration == 60);
    assert(console_buffer_used == 0);
    ++timer_starts;
    return 0;
}

int svc_timer_active(int id)
{
    assert(id == 1);
    if (finish_game) {
        /* Arrange a missed ball on the next real game-loop update. */
        assert(state == PLAYING);
        ball_row = MAXIMUM_ROW - 1;
        ball_column = MINIMUM_COLUMN + 1;
        ball_dx = 1;
        ball_dy = 1;
        lives = 1;
        return 0;
    }
    return 1;
}

unsigned svc_random_u16(void)
{
    return 0;
}

static void reset_test(void)
{
    console_buffer_used = output_used = 0;
    key_count = key_position = input_calls = 0;
    arriving_count = arrival_offset = timer_starts = 0;
    finish_game = 0;
    speaker_present = 1;
    sound_calls = sound_stops = last_preset = last_priority = 0;
    sound_available = sound_enabled = true;
    initialize_game();
    state = WAITING_TO_LAUNCH;
    paddle_column = (SCREEN_WIDTH - PADDLE_WIDTH) >> 1;
    ball_column = paddle_column + (PADDLE_WIDTH >> 1);
    ball_row = PADDLE_ROW - 1;
    ball_dx = 1;
    ball_dy = -1;
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
    write_text("\033[31m$");
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
    cursor_move(5, 17);
    set_color(107);
    write_text("  ");
    reset_color();
    write_text("$\r\n");
    write_unsigned(65535);
    write_unsigned(0);
    assert(output_used == 0);
    console_flush();
    expect_output("\033[0m\033[2J\033[H\033[?25l"
                  "\033[5;17H\033[107m  \033[0m$\r\n655350");
}

static void test_keys_during_drawing(void)
{
    reset_test();
    arriving_keys[0] = LEFT_KEY;
    arriving_keys[1] = RIGHT_KEY;
    arriving_count = 2;
    arrival_offset = 100;
    draw_border();
    draw_all_bricks();
    console_flush();
    assert(output_used > CONSOLE_BUFFER_SIZE);
    assert(key_count == 2 && key_position == 0 && input_calls == 0);
    process_input();
    assert(paddle_column == 31 && ball_column == 35);
    process_input();
    assert(paddle_column == 36 && ball_column == 40);
    assert(key_position == 2 && !quit_requested);
    console_flush();

    reset_test();
    state = PLAYING;
    arriving_keys[0] = RIGHT_KEY;
    arriving_count = 1;
    arrival_offset = 1;
    draw_ball();
    console_flush();
    process_input();
    assert(paddle_column == 41 && ball_column == 40);
    assert(key_position == 1);
}

static void test_launch_and_controls(void)
{
    unsigned test;
    static const int launch_keys[] = {SPACE_KEY, ENTER_KEY};
    static const int quit_keys[] = {ESCAPE_KEY, CTRL_C_KEY};

    for (test = 0; test < sizeof launch_keys / sizeof launch_keys[0]; ++test) {
        reset_test();
        keys[0] = launch_keys[test];
        key_count = 1;
        process_input();
        assert(state == PLAYING && timer_starts == 1);
        assert(last_preset == SND_ROUND_START);
        assert(ball_column == 41 && ball_row == PADDLE_ROW - 2);
        assert(output_used > 0 && console_buffer_used == 0);
        assert(!timer_expired());
    }
    for (test = 0; test < sizeof quit_keys / sizeof quit_keys[0]; ++test) {
        reset_test();
        keys[0] = quit_keys[test];
        key_count = 1;
        process_input();
        assert(quit_requested && output_used == 0 && timer_starts == 0);
    }
    reset_test();
    move_paddle(-SCREEN_WIDTH);
    assert(paddle_column == MINIMUM_COLUMN);
    move_paddle(SCREEN_WIDTH);
    assert(paddle_column == MAXIMUM_COLUMN - PADDLE_WIDTH + 1);
}

static void test_brick_updates(void)
{
    reset_test();
    state = PLAYING;
    assert(ball_delay_ms == 80);
    bricks_left = 45;
    assert(hit_brick(BRICK_TOP, BRICK_LEFT));
    assert(score == 80 && bricks_left == 44 && ball_delay_ms == 70);
    assert(!hit_brick(BRICK_TOP, BRICK_LEFT));
    bricks_left = 12;
    assert(hit_brick(BRICK_TOP + 1, BRICK_LEFT));
    assert(score == 150 && bricks_left == 11 && ball_delay_ms == 60);
    bricks_left = 1;
    assert(hit_brick(BRICK_TOP + 2, BRICK_LEFT));
    assert(state == GAME_WON && bricks_left == 0 && score == 210);
    assert(console_buffer_used > 0 && output_used == 0);
    console_flush();
    timer_start();
}

static void test_sound_effects_and_toggle(void)
{
    unsigned test;
    static const int toggle_keys[] = {'S', 's'};

    reset_test();
    state = PLAYING;
    ball_row = BRICK_TOP;
    ball_column = BRICK_LEFT;
    resolve_brick_collision(BRICK_TOP + 1, BRICK_LEFT + 1);
    assert(ball_dy == 1 && sound_calls == 1 && last_preset == SND_BRICK && last_priority == 2);

    reset_test();
    state = PLAYING;
    ball_row = BRICK_TOP;
    ball_column = BRICK_LEFT + BRICK_WIDTH;
    resolve_brick_collision(BRICK_TOP + 1, BRICK_LEFT + BRICK_WIDTH - 1);
    assert(!bricks[0][0] && sound_calls == 1 && last_preset == SND_BRICK);

    reset_test();
    state = PLAYING;
    ball_column = MINIMUM_COLUMN + 1;
    ball_row = 20;
    ball_dx = -1;
    move_ball();
    assert(sound_calls == 1 && last_preset == SND_WALL);
    ball_row = PADDLE_ROW - 1;
    ball_column = paddle_column;
    ball_dy = 1;
    assert(paddle_hit() && last_preset == SND_PADDLE);
    lose_ball();
    assert(last_preset == SND_BALL_LOST && last_priority == 3);
    lives = 1;
    lose_ball();
    assert(last_preset == SND_GAME_OVER && state == GAME_OVER);

    for (test = 0; test < sizeof toggle_keys / sizeof toggle_keys[0]; ++test) {
        reset_test();
        keys[0] = toggle_keys[test];
        key_count = 1;
        process_input();
        console_flush();
        output[output_used] = '\0';
        assert(!sound_enabled && sound_stops == 1);
        assert(strstr((const char *)output, "S:SOUND OFF") != NULL);
        play_sound(2, SND_BRICK);
        assert(sound_calls == 0);
        keys[1] = toggle_keys[test];
        key_count = 2;
        process_input();
        assert(sound_enabled);
        play_sound(2, SND_BRICK);
        assert(sound_calls == 1);
    }
    reset_test();
    sound_available = sound_enabled = false;
    keys[0] = 's';
    key_count = 1;
    process_input();
    console_flush();
    output[output_used] = '\0';
    assert(!sound_enabled && sound_stops == 0);
    assert(strstr((const char *)output, "S:NO SPEAKER") != NULL);
}

static void test_ball_bounces_from_contact_cell(void)
{
    const uint8_t bottom = BRICK_TOP + BRICK_ROWS - 1;

    /* Straight hit: the row above is already cleared, as in a tunnel. */
    reset_test();
    state = PLAYING;
    bricks[BRICK_ROWS - 2][0] = 0;
    ball_row = bottom + 1;
    ball_column = BRICK_LEFT + 2;
    ball_dx = -1;
    ball_dy = -1;
    move_ball();
    console_flush();
    assert(!bricks[BRICK_ROWS - 1][0] && bricks_left == BRICK_ROWS * BRICK_COLUMNS - 1);
    assert(ball_row == bottom + 1 && ball_column == BRICK_LEFT + 2);
    assert(ball_dx == -1 && ball_dy == 1);
    move_ball();
    assert(ball_row == bottom + 2 && ball_column == BRICK_LEFT + 1);

    /* Corner hit: the target is the gap between bricks; the ball stays beside it. */
    reset_test();
    state = PLAYING;
    ball_row = bottom + 1;
    ball_column = BRICK_LEFT + BRICK_WIDTH - 1;
    ball_dx = 1;
    ball_dy = -1;
    move_ball();
    console_flush();
    assert(!bricks[BRICK_ROWS - 1][0] && bricks[BRICK_ROWS - 1][1]);
    assert(ball_row == bottom + 1 && ball_column == BRICK_LEFT + BRICK_WIDTH - 1);
    assert(ball_dy == 1 && ball_dx == 1);

    /* No hit: the ball moves normally into an empty slot. */
    reset_test();
    state = PLAYING;
    bricks[BRICK_ROWS - 1][0] = 0;
    ball_row = bottom + 1;
    ball_column = BRICK_LEFT + 2;
    ball_dx = -1;
    ball_dy = -1;
    move_ball();
    assert(ball_row == bottom && ball_column == BRICK_LEFT + 1 && ball_dy == -1);
    assert(bricks_left == BRICK_ROWS * BRICK_COLUMNS);
}

static void test_main_flushes(void)
{
    static const char ending[] = "\033[0m\033[30;1H\033[?25h\r\n";
    unsigned test;

    for (test = 0; test < 2; ++test) {
        reset_test();
        finish_game = (int)test;
        arriving_keys[0] = finish_game ? SPACE_KEY : ESCAPE_KEY;
        arriving_count = 1;
        arrival_offset = 100;
        assert(breakout_main() == 0);
        assert(key_position == 1 && console_buffer_used == 0);
        assert(timer_starts == (finish_game ? 3 : 1));
        assert(output_used > sizeof ending - 1 && output_used < sizeof output);
        assert(memcmp(output + output_used - (sizeof ending - 1),
                      ending, sizeof ending - 1) == 0);
        output[output_used] = '\0';
        assert(strstr((const char *)output, "FINAL SCORE: 0") != NULL);
        if (finish_game) {
            assert(state == GAME_OVER && lives == 0);
            assert(last_preset == SND_GAME_OVER);
            assert(strstr((const char *)output, "GAME OVER") != NULL);
        } else {
            assert(quit_requested && state == WAITING_TO_LAUNCH && sound_stops == 1);
        }
    }
    reset_test();
    speaker_present = 0;
    arriving_keys[0] = ESCAPE_KEY;
    arriving_count = 1;
    arrival_offset = 100;
    assert(breakout_main() == 0);
    output[output_used] = '\0';
    assert(!sound_available && !sound_enabled && sound_calls == 0 && sound_stops == 0);
    assert(strstr((const char *)output, "S:NO SPEAKER") != NULL);
}

int main(void)
{
    test_buffer_boundaries();
    test_render_bytes();
    test_keys_during_drawing();
    test_launch_and_controls();
    test_brick_updates();
    test_sound_effects_and_toggle();
    test_ball_bounces_from_contact_cell();
    test_main_flushes();
    puts("Breakout raw console tests PASS");
    return 0;
}
