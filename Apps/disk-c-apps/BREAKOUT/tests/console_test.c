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
static int keys[64];
static unsigned key_read_output[64];
static unsigned key_count, key_position, input_calls;
static int arriving_keys[64];
static unsigned arriving_count, arrival_offset;
static unsigned timer_starts;
static int timer_column;
static unsigned timer_output;
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
    if (key_position < key_count) {
        key_read_output[key_position] = output_used;
        return keys[key_position++];
    }
    return 0;
}

int svc_start_timer(int id, unsigned duration)
{
    assert(id == 1 && duration == ball_delay_ms);
    assert(duration == INITIAL_BALL_DELAY_MS || duration == MEDIUM_BALL_DELAY_MS ||
           duration == FAST_BALL_DELAY_MS || duration == TOP_BALL_DELAY_MS);
    assert(console_buffer_used == 0);
    timer_column = ball_column;
    timer_output = output_used;
    ++timer_starts;
    return 0;
}

int svc_timer_active(int id)
{
    assert(id == 1);
    assert(!help_visible);
    if (finish_game) {
        /* Arrange a missed ball on the next real game-loop update. */
        assert(state == PLAYING);
        ball_row = MAXIMUM_ROW - 1;
        ball_column = MINIMUM_COLUMN + 1;
        ball_dx = 1;
        ball_dy = BALL_SCALE;
        ball_x = ball_column * BALL_SCALE;
        ball_y = ball_row * BALL_SCALE;
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
    ball_dx = BALL_SCALE;
    ball_dy = -BALL_SCALE;
    ball_x = ball_column * BALL_SCALE;
    ball_y = ball_row * BALL_SCALE;
}

static void place_ball(int row, int column, int dx, int dy)
{
    ball_row = row;
    ball_column = column;
    ball_x = column * BALL_SCALE;
    ball_y = row * BALL_SCALE;
    ball_dx = dx;
    ball_dy = dy;
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
        assert(output_used == length &&
               input_calls == (length + INPUT_POLL_BYTES - 1) / INPUT_POLL_BYTES);
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
    assert(key_count == 2 && key_position == 2 && input_count == 2);
    assert(key_read_output[0] >= arrival_offset &&
           key_read_output[0] - arrival_offset < INPUT_POLL_BYTES);
    assert(key_read_output[1] - arrival_offset < 2 * INPUT_POLL_BYTES);
    assert(paddle_column == 36);
    process_input();
    assert(paddle_column == 33 && ball_column == 37);
    assert(console_buffer_used == 0);
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
    assert(input_count == 1 && paddle_column == 36);
    assert(key_read_output[0] - arrival_offset < INPUT_POLL_BYTES);
    process_input();
    assert(paddle_column == 39 && ball_column == 40);
    assert(console_buffer_used == 0);
    assert(key_position == 1);
}

static void test_input_fifo_backpressure_and_batches(void)
{
    unsigned index;

    reset_test();
    state = PLAYING;
    for (index = 0; index < 24; ++index)
        keys[key_count++] = index & 1 ? RIGHT_KEY : LEFT_KEY;
    for (index = 0; index < 1024; ++index)
        console_put_byte('.');
    console_flush();
    assert(input_count == INPUT_QUEUE_SIZE && key_position == INPUT_QUEUE_SIZE);
    assert(input_calls == INPUT_QUEUE_SIZE && paddle_column == 36);
    assert(output_used == 1024);
    for (index = 0; index < output_used; ++index)
        assert(output[index] == '.');

    process_input_batch();
    assert(paddle_column == 36 && input_count == INPUT_QUEUE_SIZE);
    assert(key_position == 24 && timer_starts == 0);
    process_input_batch();
    assert(paddle_column == 36 && input_count == INPUT_QUEUE_SIZE - INPUT_BATCH_SIZE);
    process_input_batch();
    assert(paddle_column == 36 && input_count == 0 && key_position == 24);
    assert(!quit_requested && console_buffer_used == 0);

    reset_test();
    state = PLAYING;
    keys[0] = LEFT_KEY;
    keys[1] = ESCAPE_KEY;
    keys[2] = RIGHT_KEY;
    key_count = 3;
    process_input_batch();
    assert(quit_requested && paddle_column == 33 && timer_starts == 0);
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
        assert(timer_column == 40 && timer_output < output_used);
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
    assert(ball_delay_ms == 70);
    assert(hit_brick(BRICK_TOP + 7, BRICK_LEFT));
    assert(score == 1 && ball_delay_ms == 70);
    assert(!hit_brick(BRICK_TOP + 7, BRICK_LEFT));
    assert(hit_brick(BRICK_TOP + 5, BRICK_LEFT));
    assert(score == 4 && ball_delay_ms == 70);
    assert(hit_brick(BRICK_TOP + 3, BRICK_LEFT));
    assert(score == 9 && ball_delay_ms == 55);
    assert(hit_brick(BRICK_TOP, BRICK_LEFT));
    assert(score == 16 && ball_delay_ms == 50);
    bricks_left = 1;
    assert(hit_brick(BRICK_TOP + 6, BRICK_LEFT));
    assert(state == GAME_WON && bricks_left == 0 && score == 17);
    assert(ball_delay_ms == 50);
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
    assert(ball_dy == BALL_SCALE && sound_calls == 1 && last_preset == SND_BRICK && last_priority == 2);

    reset_test();
    state = PLAYING;
    ball_row = BRICK_TOP;
    ball_column = BRICK_LEFT + BRICK_WIDTH;
    resolve_brick_collision(BRICK_TOP + 1, BRICK_LEFT + BRICK_WIDTH - 1);
    assert(!bricks[0][0] && sound_calls == 1 && last_preset == SND_BRICK);

    reset_test();
    state = PLAYING;
    place_ball(20, MINIMUM_COLUMN + 1, -BALL_SCALE, -BALL_SCALE);
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
    place_ball(bottom + 1, BRICK_LEFT + 2, -BALL_SCALE, -BALL_SCALE);
    move_ball();
    console_flush();
    assert(!bricks[BRICK_ROWS - 1][0] && bricks_left == BRICK_ROWS * BRICK_COLUMNS - 1);
    assert(ball_row == bottom + 1 && ball_column == BRICK_LEFT + 2);
    assert(ball_dx == -BALL_SCALE && ball_dy == BALL_SCALE);
    move_ball();
    assert(ball_row == bottom + 2 && ball_column == BRICK_LEFT + 1);

    /* Corner hit: the target is the gap between bricks; the ball stays beside it. */
    reset_test();
    state = PLAYING;
    place_ball(bottom + 1, BRICK_LEFT + BRICK_WIDTH - 1, BALL_SCALE, -BALL_SCALE);
    move_ball();
    console_flush();
    assert(!bricks[BRICK_ROWS - 1][0] && bricks[BRICK_ROWS - 1][1]);
    assert(ball_row == bottom + 1 && ball_column == BRICK_LEFT + BRICK_WIDTH - 1);
    assert(ball_dy == BALL_SCALE && ball_dx == BALL_SCALE);

    /* No hit: the ball moves normally into an empty slot. */
    reset_test();
    state = PLAYING;
    bricks[BRICK_ROWS - 1][0] = 0;
    place_ball(bottom + 1, BRICK_LEFT + 2, -BALL_SCALE, -BALL_SCALE);
    move_ball();
    assert(ball_row == bottom && ball_column == BRICK_LEFT + 1 && ball_dy == -BALL_SCALE);
    assert(bricks_left == BRICK_ROWS * BRICK_COLUMNS);
}

static void test_paddle_angles_and_acceleration(void)
{
    int column;
    int previous_dx = -BALL_SCALE - 1;
    unsigned hit;

    reset_test();
    state = PLAYING;
    for (column = 0; column < PADDLE_WIDTH; ++column) {
        place_ball(PADDLE_ROW - 1, paddle_column + column, 5, 7);
        assert(paddle_hit());
        assert(ball_dx >= previous_dx && ball_dx != 0);
        assert(abs(ball_dx) <= BALL_SCALE && ball_dy < 0 &&
               abs(ball_dy) <= BALL_SCALE);
        if (column == 0 || column == PADDLE_WIDTH - 1)
            assert(ball_dy == -5);
        if (column == 3 || column == 4)
            assert(ball_dy == -8);
        previous_dx = ball_dx;
    }
    assert(score == 0);
    reset_test();
    for (hit = 1; hit <= 15; ++hit) {
        place_ball(PADDLE_ROW - 1, paddle_column, 5, 7);
        assert(paddle_hit());
        assert(ball_delay_ms == (hit < 4 ? 70 : hit < 12 ? 60 : 55));
    }
    assert(rally_hits == 12);
    ball_delay_ms = TOP_BALL_DELAY_MS;
    ball_dy = 7;
    assert(paddle_hit() && ball_delay_ms == TOP_BALL_DELAY_MS);
    for (column = -1; column <= PADDLE_WIDTH; column += PADDLE_WIDTH + 1) {
        place_ball(PADDLE_ROW - 1, paddle_column + column, 5, 7);
        assert(!paddle_hit());
    }
}

static void test_ceiling_keeps_full_paddle_and_serve_reset(void)
{
    int old_column;
    int column;

    reset_test();
    state = PLAYING;
    old_column = paddle_column;
    draw_paddle();
    console_flush();
    expect_output("\033[45m\033[29;36H        \033[0m");
    output_used = 0;
    place_ball(MINIMUM_ROW + 1, 40, 2, -8);
    move_ball();
    assert(ball_dy == 8 && ball_row == MINIMUM_ROW);
    assert(paddle_column == old_column);
    assert(ball_delay_ms == 50);
    console_flush();
    output[output_used] = '\0';
    assert(!strstr((const char *)output, "\033[29;"));
    for (column = 0; column < PADDLE_WIDTH; ++column) {
        place_ball(PADDLE_ROW - 1, paddle_column + column, 2, 8);
        assert(paddle_hit() && ball_dy < 0 && ball_dx != 0);
    }
    place_ball(MINIMUM_ROW + 1, 40, 2, -8);
    move_ball();
    assert(paddle_column == old_column);
    move_paddle(SCREEN_WIDTH);
    assert(paddle_column == MAXIMUM_COLUMN - PADDLE_WIDTH + 1);
    lose_ball();
    assert(ball_delay_ms == 70 && rally_hits == 0);
    assert(ball_x == ball_column * BALL_SCALE && ball_y == ball_row * BALL_SCALE);
    assert(state == WAITING_TO_LAUNCH);
}

static void test_fractional_motion_and_side_collision(void)
{
    unsigned tick;
    const int gap = BRICK_LEFT + BRICK_WIDTH;

    reset_test();
    state = PLAYING;
    place_ball(20, 40, 2, -5);
    for (tick = 0; tick < 4; ++tick)
        move_ball();
    assert(ball_x == 41 * BALL_SCALE && ball_y == 20 * BALL_SCALE - 20);
    assert(ball_column == 41 && ball_row == 17);

    reset_test();
    state = PLAYING;
    place_ball(BRICK_TOP, gap, -8, -8);
    move_ball();
    assert(!bricks[0][0] && ball_dx == 8 && ball_dy == -8);
    assert(ball_row == BRICK_TOP && ball_column == gap);
    assert(ball_x == gap * BALL_SCALE && ball_y == BRICK_TOP * BALL_SCALE);

    /* A diagonal corner with neither axial neighbour occupied reverses both axes. */
    reset_test();
    state = PLAYING;
    place_ball(BRICK_TOP + BRICK_ROWS, gap, -8, -8);
    move_ball();
    assert(!bricks[BRICK_ROWS - 1][0] && ball_dx == 8 && ball_dy == 8);

    /* A sub-cell tick with no visible change emits no redraw. */
    reset_test();
    state = PLAYING;
    place_ball(20, 40, 2, 5);
    move_ball();
    assert(console_buffer_used == 0 && output_used == 0);
}

static void test_full_wall_score_and_paddle_miss(void)
{
    unsigned row, column;

    reset_test();
    state = PLAYING;
    for (row = 0; row < BRICK_ROWS; ++row)
        for (column = 0; column < BRICK_COLUMNS; ++column)
            assert(hit_brick(BRICK_TOP + row,
                             BRICK_LEFT + column * (BRICK_WIDTH + BRICK_GAP)));
    assert(score == 416 && bricks_left == 0 && state == GAME_WON);
    console_flush();
    reset_test();
    state = PLAYING;
    place_ball(PADDLE_ROW - 2, paddle_column - 1, 0, BALL_SCALE);
    move_ball();
    assert(ball_dy == BALL_SCALE && lives == 4);
    move_ball();
    move_ball();
    assert(lives == 3 && state == WAITING_TO_LAUNCH);
}

static void press_key(int key)
{
    assert(key_count < sizeof keys / sizeof keys[0]);
    keys[key_count++] = key;
    process_input();
}

static void test_help_pause_and_restore(void)
{
    static const int dismiss_keys[] = {'h', 'H', SPACE_KEY, ENTER_KEY};
    unsigned test;
    uint8_t saved_bricks[BRICK_ROWS][BRICK_COLUMNS];

    for (test = 0; test < sizeof dismiss_keys / sizeof dismiss_keys[0]; ++test) {
        reset_test();
        state = PLAYING;
        place_ball(20, 40, -5, 7);
        ball_x += 3;
        ball_y += 2;
        rally_hits = 12;
        ball_delay_ms = TOP_BALL_DELAY_MS;
        score = 7;
        lives = 2;
        bricks[0][0] = 0;
        bricks_left--;
        memcpy(saved_bricks, bricks, sizeof bricks);
        press_key(test & 1 ? 'H' : 'h');
        assert(help_visible && state == PLAYING && timer_starts == 0);
        assert(console_buffer_used == 0);
        output[output_used] = '\0';
        assert(strstr((const char *)output, "BREAKOUT HELP - GAME PAUSED"));
        assert(strstr((const char *)output, "Left / Right arrows"));
        assert(strstr((const char *)output, "Escape / Ctrl-C"));
        assert(strstr((const char *)output, "H / Space / Enter"));
        move_ball();
        press_key(LEFT_KEY);
        press_key(RIGHT_KEY);
        assert(ball_x == 323 && ball_y == 162 && paddle_column == 36);
        assert(sound_calls == 0 && timer_starts == 0);
        press_key('s');
        assert(!sound_enabled && sound_stops == 1 && help_visible);
        press_key('S');
        assert(sound_enabled && help_visible);
        press_key(dismiss_keys[test]);
        assert(!help_visible && state == PLAYING && timer_starts == 1);
        assert(ball_x == 323 && ball_y == 162 && ball_dx == -5 && ball_dy == 7);
        assert(ball_column == 40 && ball_row == 20 && paddle_column == 36);
        assert(score == 7 && lives == 2 && rally_hits == 12);
        assert(ball_delay_ms == TOP_BALL_DELAY_MS);
        assert(memcmp(saved_bricks, bricks, sizeof bricks) == 0);
        assert(bricks_left == BRICK_ROWS * BRICK_COLUMNS - 1);
        assert(console_buffer_used == 0 && sound_calls == 0);
    }
    reset_test();
    press_key('h');
    press_key(SPACE_KEY);
    assert(state == WAITING_TO_LAUNCH && !help_visible && sound_calls == 0);
    output[output_used] = '\0';
    assert(strstr((const char *)output, "SPACE TO LAUNCH"));
    press_key(ENTER_KEY);
    assert(state == PLAYING && last_preset == SND_ROUND_START);

    reset_test();
    press_key('h');
    press_key(ESCAPE_KEY);
    assert(quit_requested && help_visible);
}

static void test_start_screen_and_help_loop(void)
{
    reset_test();
    state = START_SCREEN;
    draw_start_screen();
    output[output_used] = '\0';
    assert(strstr((const char *)output, "B R E A K O U T"));
    assert(strstr((const char *)output, "SMASH. BOUNCE. REPEAT!"));
    assert(strstr((const char *)output, "Space / Enter: play"));
    assert(strstr((const char *)output, "H: help"));
    assert(strstr((const char *)output, "\033[0m\033[2J"));
    assert(!strstr((const char *)output, "\033[0;40m\033[2J"));
    assert(strstr((const char *)output, "\033[40m\033[3;3H"));
    assert(strstr((const char *)output, "\033[7;8H\033[101m    "));
    assert(strstr((const char *)output, "\033[9;8H\033[43m    "));
    assert(strstr((const char *)output, "\033[11;8H\033[42m    "));
    assert(strstr((const char *)output, "\033[13;8H\033[103m    "));
    assert(!strstr((const char *)output, "\033[13;23H"));
    assert(strstr((const char *)output, "\033[16;47HO"));
    assert(strstr((const char *)output, "\033[22;36H\033[105m        "));
    assert(score == 0 && lives == 4 && bricks_left == BRICK_ROWS * BRICK_COLUMNS);
    for (unsigned row = 0; row < BRICK_ROWS; ++row)
        for (unsigned column = 0; column < BRICK_COLUMNS; ++column)
            assert(bricks[row][column] == 1);
    press_key(LEFT_KEY);
    assert(state == START_SCREEN && paddle_column == 36);
    press_key('h');
    press_key(ENTER_KEY);
    assert(state == START_SCREEN && !help_visible && sound_calls == 0);
    press_key(SPACE_KEY);
    assert(state == PLAYING && last_preset == SND_ROUND_START);

    reset_test();
    arriving_keys[0] = 'h';
    arriving_keys[1] = 'H';
    arriving_keys[2] = SPACE_KEY;
    arriving_keys[3] = 'h';
    arriving_keys[4] = LEFT_KEY;
    arriving_keys[5] = 's';
    arriving_keys[6] = ENTER_KEY;
    arriving_keys[7] = ESCAPE_KEY;
    arriving_count = 8;
    arrival_offset = 100;
    assert(breakout_main() == 0);
    assert(key_position == 8 && timer_starts == 4 && !help_visible);
    assert(state == PLAYING && quit_requested && sound_calls == 2 && sound_stops == 2);
    assert(paddle_column == 36 && !sound_enabled && console_buffer_used == 0);
}

static void test_start_palette_is_bounded_and_cleared(void)
{
    reset_test();
    state = START_SCREEN;
    set_color(40);
    draw_status();
    console_flush();
    output[output_used] = '\0';
    assert(strstr((const char *)output, "\033[0m\033[37m\033[1;2HSCORE:0\033[K"));

    output_used = 0;
    draw_start_screen();
    output_used = 0;
    press_key(SPACE_KEY);
    output[output_used] = '\0';
    assert(state == PLAYING);
    assert(!strstr((const char *)output, "\033[40m"));
    assert(strstr((const char *)output, "\033[0m\033[2J"));
    assert(strstr((const char *)output, "\033[0m\033[37m\033[1;2HSCORE:0\033[K"));
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
            assert(quit_requested && state == START_SCREEN && sound_stops == 1);
            assert(sound_calls == 1 && last_preset == SND_FANFARE && last_priority == 1);
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
    test_input_fifo_backpressure_and_batches();
    test_launch_and_controls();
    test_brick_updates();
    test_sound_effects_and_toggle();
    test_ball_bounces_from_contact_cell();
    test_paddle_angles_and_acceleration();
    test_ceiling_keeps_full_paddle_and_serve_reset();
    test_fractional_motion_and_side_collision();
    test_full_wall_score_and_paddle_miss();
    test_help_pause_and_restore();
    test_start_screen_and_help_loop();
    test_start_palette_is_bounded_and_cleared();
    test_main_flushes();
    puts("Breakout raw console tests PASS");
    return 0;
}
