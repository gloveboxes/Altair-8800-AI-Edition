/* Breakout for dcc C11, CP/M 2.2, and the Altair 8800 emulator. */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ESCAPE_KEY 27
#define CTRL_C_KEY 3
#define SPACE_KEY 32
#define ENTER_KEY 13
#define RIGHT_KEY 4
#define LEFT_KEY 19

#include "../COMMON/DXSVC.H"
#define INITIAL_BALL_DELAY_MS 70
#define MEDIUM_BALL_DELAY_MS 60
#define FAST_BALL_DELAY_MS 55
#define TOP_BALL_DELAY_MS 50
#define BALL_SCALE 8

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 30
#define MINIMUM_ROW 3
#define MAXIMUM_ROW 30
#define MINIMUM_COLUMN 3
#define MAXIMUM_COLUMN 78

#define BRICK_ROWS 8
#define BRICK_COLUMNS 13
#define BRICK_WIDTH 4
#define BRICK_GAP 1
#define BRICK_TOP 7
#define BRICK_LEFT 8

#define PADDLE_ROW 29
#define PADDLE_WIDTH 8
#define PADDLE_STEP 3
#define CONSOLE_BUFFER_SIZE 4096
#define INPUT_QUEUE_SIZE 16
#define INPUT_POLL_BYTES 16
#define INPUT_BATCH_SIZE 8
#define NO_BRICK 0xff

enum game_state {
    START_SCREEN,
    WAITING_TO_LAUNCH,
    PLAYING,
    GAME_OVER,
    GAME_WON
};

static uint8_t console_buffer[CONSOLE_BUFFER_SIZE];
static unsigned console_buffer_used;
static uint8_t input_queue[INPUT_QUEUE_SIZE];
static uint8_t input_head;
static uint8_t input_tail;
static uint8_t input_count;
static uint8_t bricks[BRICK_ROWS][BRICK_COLUMNS];
static uint8_t brick_column_at[SCREEN_WIDTH + 1];

static int ball_column;
static int ball_row;
static int ball_dx;
static int ball_dy;
static int ball_x;
static int ball_y;
static int paddle_column;
static uint8_t rally_hits;
static unsigned score;
static uint8_t lives;
static uint8_t bricks_left;
static enum game_state state;
static uint8_t ball_delay_ms;
static bool quit_requested;
static bool sound_enabled;
static bool sound_available;
static bool help_visible;

static void poll_input(void)
{
    int key;

    if (input_count == INPUT_QUEUE_SIZE || quit_requested ||
        state == GAME_OVER || state == GAME_WON)
        return;
    key = bdos(6, 0xff) & 0xff;
    if (key != 0) {
        input_queue[input_tail] = (uint8_t)key;
        input_tail = (input_tail + 1) & (INPUT_QUEUE_SIZE - 1);
        input_count++;
    }
}

static void console_flush(void)
{
    unsigned index;

    /* BIOS CONOUT bypasses BDOS look-ahead/flow control for every byte. */
    for (index = 0; index < console_buffer_used; ++index) {
        bios(4, console_buffer[index]);
        /* Capture keys without re-entering drawing in the middle of ANSI output. */
        if ((index & (INPUT_POLL_BYTES - 1)) == INPUT_POLL_BYTES - 1)
            poll_input();
    }
    if (console_buffer_used & (INPUT_POLL_BYTES - 1))
        poll_input();
    console_buffer_used = 0;
}

static inline void console_put_byte(uint8_t byte)
{
    if (console_buffer_used == CONSOLE_BUFFER_SIZE)
        console_flush();
    console_buffer[console_buffer_used++] = byte;
}

static void write_text(const char *text)
{
    uint8_t byte;

    while (*text != '\0') {
        byte = (unsigned char)*text++;
        console_put_byte(byte);
    }
}

static void write_unsigned(unsigned value)
{
    char digits[5];
    uint8_t count = 0;
    uint8_t byte;

    if (value == 0) {
        console_put_byte('0');
        return;
    }
    while (value != 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count != 0) {
        byte = digits[--count];
        console_put_byte(byte);
    }
}

static void cursor_move(uint8_t row, uint8_t column)
{
    console_put_byte(ESCAPE_KEY);
    console_put_byte('[');
    write_unsigned(row);
    console_put_byte(';');
    write_unsigned(column);
    console_put_byte('H');
}

static void set_color(uint8_t color)
{
    console_put_byte(ESCAPE_KEY);
    console_put_byte('[');
    write_unsigned(color);
    console_put_byte('m');
}

static inline void reset_color(void)
{
    write_text("\033[0m");
}

static inline void clear_screen(void)
{
    write_text("\033[0m\033[2J\033[H\033[?25l");
}

static inline void erase_to_end_of_line(void)
{
    write_text("\033[K");
}

static inline void timer_start(void)
{
    svc_start_timer(1, ball_delay_ms);
}

/* Higher-priority cues survive less important collisions during playback. */
static void play_sound(int priority, int preset)
{
    if (sound_enabled)
        snd_preset(priority, preset);
}

static inline bool timer_expired(void)
{
    return svc_timer_active(1) == 0;
}

/* The utility service is absent on some hosts, so use input-poll jitter. */
static unsigned entropy;

static inline unsigned random_word(void)
{
    entropy = entropy * 25173u + 13849u;
    return entropy >> 8;
}

static int read_key(void)
{
    int key;

    if (input_count != 0) {
        key = input_queue[input_head];
        input_head = (input_head + 1) & (INPUT_QUEUE_SIZE - 1);
        input_count--;
        return key;
    }
    return bdos(6, 0xff) & 0xff;
}

static void draw_status(void)
{
    reset_color();
    set_color(37);
    cursor_move(1, 2);
    write_text("SCORE:");
    write_unsigned(score);
    erase_to_end_of_line();
    cursor_move(1, 26);
    write_text(!sound_available ? "S:NO SPEAKER" :
               sound_enabled ? "S:SOUND ON " : "S:SOUND OFF");
    cursor_move(1, 50);
    write_text("LIFE:");
    write_unsigned(lives);
    cursor_move(1, 62);
    write_text("H:HELP");
    reset_color();
}

static inline uint8_t checker_color(uint8_t row, uint8_t column)
{
    return (((row >> 1) + (column >> 1)) & 1) ? 100 : 107;
}

static void draw_border(void)
{
    uint8_t row;
    uint8_t column;

    for (column = 0; column < SCREEN_WIDTH; column += 2) {
        set_color(checker_color(0, column >> 1));
        cursor_move(2, column + 1);
        write_text("  ");
        set_color(checker_color(SCREEN_HEIGHT - 1, column >> 1));
        cursor_move(SCREEN_HEIGHT, column + 1);
        write_text("  ");
    }
    for (row = 3; row < SCREEN_HEIGHT; row++) {
        set_color(checker_color(row - 2, 0));
        cursor_move(row, 1);
        write_text("  ");
        set_color(checker_color(row - 2, (SCREEN_WIDTH >> 1) - 1));
        cursor_move(row, SCREEN_WIDTH - 1);
        write_text("  ");
    }
    reset_color();
}

static const uint8_t brick_colors[BRICK_ROWS] = {101, 101, 43, 43, 42, 42, 103, 103};

static inline uint8_t brick_color(uint8_t row)
{
    return brick_colors[row];
}

static void draw_brick(uint8_t row, uint8_t column)
{
    uint8_t screen_column = BRICK_LEFT + column * (BRICK_WIDTH + BRICK_GAP);
    uint8_t width;

    cursor_move(BRICK_TOP + row, screen_column);
    if (bricks[row][column])
        set_color(brick_color(row));
    else
        reset_color();
    for (width = 0; width < BRICK_WIDTH; width++)
        console_put_byte(' ');
    reset_color();
}

static void draw_all_bricks(void)
{
    uint8_t row;
    uint8_t column;

    for (row = 0; row < BRICK_ROWS; row++)
        for (column = 0; column < BRICK_COLUMNS; column++)
            draw_brick(row, column);
}

static void draw_paddle(void)
{
    uint8_t width;

    set_color(45);
    cursor_move(PADDLE_ROW, paddle_column);
    for (width = 0; width < PADDLE_WIDTH; width++)
        console_put_byte(' ');
    reset_color();
}

static void erase_paddle(void)
{
    uint8_t width;

    cursor_move(PADDLE_ROW, paddle_column);
    for (width = 0; width < PADDLE_WIDTH; width++)
        console_put_byte(' ');
}

static void draw_ball(void)
{
    set_color(97);
    cursor_move(ball_row, ball_column);
    console_put_byte('O');
    reset_color();
}

static void restore_cell(uint8_t row, uint8_t column)
{
    int brick_row = row - BRICK_TOP;
    uint8_t brick_column;

    if (brick_row >= 0 && brick_row < BRICK_ROWS) {
        brick_column = brick_column_at[column];
        if (brick_column != NO_BRICK) {
            draw_brick((uint8_t)brick_row, brick_column);
            return;
        }
    }
    if (row == PADDLE_ROW && column >= paddle_column &&
        column < paddle_column + PADDLE_WIDTH) {
        draw_paddle();
        return;
    }
    reset_color();
    cursor_move(row, column);
    console_put_byte(' ');
}

static void show_message(const char *message)
{
    uint8_t column;

    reset_color();
    cursor_move(23, MINIMUM_COLUMN);
    for (column = MINIMUM_COLUMN; column <= MAXIMUM_COLUMN; column++)
        console_put_byte(' ');
    set_color(37);
    cursor_move(23, 20);
    write_text(message);
    reset_color();
}

static void clear_message(void)
{
    uint8_t column;

    reset_color();
    cursor_move(23, MINIMUM_COLUMN);
    for (column = MINIMUM_COLUMN; column <= MAXIMUM_COLUMN; column++)
        console_put_byte(' ');
}

static void initialize_game(void)
{
    uint8_t column;
    uint8_t screen_column;

    memset(bricks, 1, sizeof(bricks));
    memset(brick_column_at, NO_BRICK, sizeof(brick_column_at));
    for (column = 0; column < BRICK_COLUMNS; column++) {
        screen_column = BRICK_LEFT + column * (BRICK_WIDTH + BRICK_GAP);
        brick_column_at[screen_column] = column;
        brick_column_at[screen_column + 1] = column;
        brick_column_at[screen_column + 2] = column;
        brick_column_at[screen_column + 3] = column;
    }
    score = 0;
    lives = 4;
    bricks_left = BRICK_ROWS * BRICK_COLUMNS;
    ball_delay_ms = INITIAL_BALL_DELAY_MS;
    rally_hits = 0;
    quit_requested = false;
    input_head = input_tail = input_count = 0;
    help_visible = false;
    state = START_SCREEN;
}

static void serve_ball(void)
{
    rally_hits = 0;
    ball_delay_ms = INITIAL_BALL_DELAY_MS;
    paddle_column = (SCREEN_WIDTH - PADDLE_WIDTH) >> 1;
    ball_column = paddle_column + (PADDLE_WIDTH >> 1);
    ball_row = PADDLE_ROW - 1;
    ball_x = ball_column * BALL_SCALE;
    ball_y = ball_row * BALL_SCALE;
    ball_dx = (random_word() & 1) ? -5 : 5;
    ball_dy = -7;
    state = WAITING_TO_LAUNCH;
    draw_paddle();
    draw_ball();
    show_message("SPACE TO LAUNCH");
}

static bool paddle_hit(void)
{
    int relative_column;
    int zone;
    static const int rebound_dx[6] = {-8, -5, -2, 2, 5, 8};
    static const int rebound_dy[6] = {-5, -7, -8, -8, -7, -5};

    if (ball_dy <= 0 || ball_row != PADDLE_ROW - 1 ||
        ball_column < paddle_column || ball_column >= paddle_column + PADDLE_WIDTH)
        return false;
    relative_column = ball_column - paddle_column;
    zone = (relative_column * 2 + 1) * 6 / (PADDLE_WIDTH * 2);
    ball_dx = rebound_dx[zone];
    ball_dy = rebound_dy[zone];
    ball_y = ball_row * BALL_SCALE;
    if (rally_hits < 12)
        rally_hits++;
    if (rally_hits >= 12 && ball_delay_ms > FAST_BALL_DELAY_MS)
        ball_delay_ms = FAST_BALL_DELAY_MS;
    else if (rally_hits >= 4 && ball_delay_ms > MEDIUM_BALL_DELAY_MS)
        ball_delay_ms = MEDIUM_BALL_DELAY_MS;
    play_sound(1, SND_PADDLE);
    return true;
}

static bool hit_brick(uint8_t screen_row, uint8_t screen_column)
{
    int row = screen_row - BRICK_TOP;
    uint8_t column;

    if (row < 0 || row >= BRICK_ROWS)
        return false;
    column = brick_column_at[screen_column];
    if (column == NO_BRICK || !bricks[row][column])
        return false;
    bricks[row][column] = 0;
    bricks_left--;
    score += 7 - (row >> 1) * 2;
    if (row < 2 && ball_delay_ms > TOP_BALL_DELAY_MS)
        ball_delay_ms = TOP_BALL_DELAY_MS;
    else if (row < 4 && ball_delay_ms > FAST_BALL_DELAY_MS)
        ball_delay_ms = FAST_BALL_DELAY_MS;
    draw_brick((uint8_t)row, column);
    draw_status();
    if (bricks_left == 0)
        state = GAME_WON;
    return true;
}

static bool resolve_brick_collision(uint8_t previous_row, uint8_t previous_column)
{
    bool vertical_hit;
    bool horizontal_hit;

    vertical_hit = previous_row != ball_row &&
                   hit_brick((uint8_t)ball_row, previous_column);
    horizontal_hit = previous_column != ball_column &&
                     hit_brick(previous_row, (uint8_t)ball_column);
    if (vertical_hit)
        ball_dy = -ball_dy;
    if (horizontal_hit)
        ball_dx = -ball_dx;
    if (!vertical_hit && !horizontal_hit) {
        if ((previous_row == ball_row && previous_column == ball_column) ||
            !hit_brick((uint8_t)ball_row, (uint8_t)ball_column))
            return false;
        if (previous_row != ball_row)
            ball_dy = -ball_dy;
        if (previous_column != ball_column)
            ball_dx = -ball_dx;
    }
    play_sound(2, SND_BRICK);
    return true;
}

static void lose_ball(void)
{
    lives--;
    play_sound(3, lives == 0 ? SND_GAME_OVER : SND_BALL_LOST);
    draw_status();
    if (lives == 0) {
        state = GAME_OVER;
        return;
    }
    erase_paddle();
    serve_ball();
}

static void move_ball(void)
{
    uint8_t previous_column;
    uint8_t previous_row;
    int previous_x;
    int previous_y;
    bool wall_hit = false;

    if (state != PLAYING || help_visible)
        return;
    previous_column = (uint8_t)ball_column;
    previous_row = (uint8_t)ball_row;
    previous_x = ball_x;
    previous_y = ball_y;
    /* Velocities never exceed one cell per tick, so no brick can be skipped. */
    ball_x += ball_dx;
    ball_y += ball_dy;
    if (ball_x <= MINIMUM_COLUMN * BALL_SCALE) {
        ball_x = MINIMUM_COLUMN * BALL_SCALE;
        ball_dx = abs(ball_dx);
        wall_hit = true;
    } else if (ball_x >= MAXIMUM_COLUMN * BALL_SCALE) {
        ball_x = MAXIMUM_COLUMN * BALL_SCALE;
        ball_dx = -abs(ball_dx);
        wall_hit = true;
    }
    if (ball_y <= MINIMUM_ROW * BALL_SCALE) {
        ball_y = MINIMUM_ROW * BALL_SCALE;
        ball_dy = abs(ball_dy);
        wall_hit = true;
        ball_delay_ms = TOP_BALL_DELAY_MS;
    }
    ball_column = ball_x / BALL_SCALE;
    ball_row = ball_y / BALL_SCALE;
    if (wall_hit)
        play_sound(1, SND_WALL);
    paddle_hit();
    if (ball_row >= MAXIMUM_ROW) {
        restore_cell(previous_row, previous_column);
        lose_ball();
        return;
    }
    if (resolve_brick_collision(previous_row, previous_column)) {
        /* Bounce from the cell touching the brick, not inside its erased slot. */
        ball_row = previous_row;
        ball_column = previous_column;
        ball_x = previous_x;
        ball_y = previous_y;
        draw_ball();
        return;
    }
    if (previous_row != ball_row || previous_column != ball_column) {
        restore_cell(previous_row, previous_column);
        draw_ball();
    }
}

static void move_paddle(int direction)
{
    int new_column = paddle_column + direction;

    if (new_column < MINIMUM_COLUMN)
        new_column = MINIMUM_COLUMN;
    if (new_column > MAXIMUM_COLUMN - PADDLE_WIDTH + 1)
        new_column = MAXIMUM_COLUMN - PADDLE_WIDTH + 1;
    if (new_column == paddle_column)
        return;
    erase_paddle();
    paddle_column = new_column;
    draw_paddle();
    if (state == WAITING_TO_LAUNCH) {
        restore_cell((uint8_t)ball_row, (uint8_t)ball_column);
        ball_column = paddle_column + (PADDLE_WIDTH >> 1);
        ball_x = ball_column * BALL_SCALE;
        draw_ball();
    }
    console_flush();
}

static void screen_line(uint8_t row, const char *text)
{
    cursor_move(row, 10);
    write_text(text);
}

static void draw_start_screen(void)
{
    uint8_t row;
    uint8_t column;

    clear_screen();
    set_color(40);
    for (row = MINIMUM_ROW; row < SCREEN_HEIGHT; ++row) {
        cursor_move(row, MINIMUM_COLUMN);
        for (column = MINIMUM_COLUMN; column <= MAXIMUM_COLUMN; ++column)
            console_put_byte(' ');
    }
    reset_color();
    draw_border();
    draw_status();
    set_color(40);
    set_color(96);
    cursor_move(4, 32);
    write_text("B R E A K O U T");
    set_color(93);
    cursor_move(5, 29);
    write_text("SMASH. BOUNCE. REPEAT!");
    for (row = 0; row < BRICK_ROWS; ++row) {
        for (column = 0; column < BRICK_COLUMNS; ++column) {
            if ((row >= 2 && column == 6) ||
                (row >= 4 && column >= 5 && column <= 7) ||
                (row >= 6 && column >= 3 && column <= 9) ||
                (row == 7 && (column == 1 || column == 11)))
                continue;
            draw_brick(row, column);
        }
    }
    set_color(40);
    set_color(90);
    cursor_move(20, 39);
    console_put_byte('.');
    set_color(36);
    cursor_move(19, 41);
    console_put_byte('.');
    set_color(96);
    cursor_move(18, 43);
    console_put_byte(':');
    cursor_move(17, 45);
    console_put_byte('*');
    set_color(97);
    cursor_move(16, 47);
    console_put_byte('O');
    cursor_move(22, 36);
    set_color(105);
    write_text("        ");
    set_color(40);
    set_color(96);
    screen_line(25, "Space / Enter: play    H: help    S: sound");
    set_color(93);
    screen_line(26, "Four lives. One wall. How far can you go?");
    set_color(97);
    screen_line(28, "Escape / Ctrl-C: quit");
    reset_color();
    console_flush();
}

static void show_help(void)
{
    help_visible = true;
    clear_screen();
    draw_border();
    draw_status();
    set_color(37);
    screen_line(5, "BREAKOUT HELP - GAME PAUSED");
    screen_line(8, "Left / Right arrows : move the paddle");
    screen_line(9, "Space / Enter       : start / launch the ball");
    screen_line(10, "S                   : toggle sound (if available)");
    screen_line(11, "H                   : show / close this help");
    screen_line(12, "Escape / Ctrl-C     : quit the game");
    screen_line(15, "Centre paddle hits send the ball steeply upward.");
    screen_line(16, "Edge hits aim outward at a shallower angle.");
    screen_line(18, "Long rallies and upper bricks make the ball faster.");
    screen_line(19, "The paddle stays full-width, even after ceiling hits.");
    screen_line(23, "H / Space / Enter: return to the game");
    reset_color();
    console_flush();
}

static void close_help(void)
{
    help_visible = false;
    if (state == START_SCREEN) {
        draw_start_screen();
        timer_start();
        return;
    }
    clear_screen();
    draw_status();
    draw_border();
    draw_all_bricks();
    draw_paddle();
    draw_ball();
    if (state == WAITING_TO_LAUNCH)
        show_message("SPACE TO LAUNCH");
    console_flush();
    timer_start();
}

static bool process_input(void)
{
    entropy++;
    int key = read_key();

    if (key == 0)
        return false;
    if (key == ESCAPE_KEY || key == CTRL_C_KEY) {
        quit_requested = true;
        return true;
    }
    if (key == 'H' || key == 'h') {
        if (help_visible)
            close_help();
        else
            show_help();
        return true;
    }
    if (key == 'S' || key == 's') {
        if (sound_available) {
            sound_enabled = !sound_enabled;
            if (!sound_enabled)
                snd_stop();
        }
        draw_status();
        console_flush();
        return true;
    }
    if (help_visible) {
        if (key == SPACE_KEY || key == ENTER_KEY)
            close_help();
        return true;
    }
    if (key == LEFT_KEY && state != START_SCREEN)
        move_paddle(-PADDLE_STEP);
    else if (key == RIGHT_KEY && state != START_SCREEN)
        move_paddle(PADDLE_STEP);
    else if ((key == SPACE_KEY || key == ENTER_KEY) &&
             (state == WAITING_TO_LAUNCH || state == START_SCREEN)) {
        if (state == START_SCREEN) {
            state = WAITING_TO_LAUNCH;
            clear_screen();
            draw_status();
            draw_border();
            draw_all_bricks();
            serve_ball();
        }
        clear_message();
        state = PLAYING;
        console_flush();
        timer_start();
        play_sound(1, SND_ROUND_START);
        move_ball();
        console_flush();
    }
    return true;
}

static void process_input_batch(void)
{
    uint8_t count;

    for (count = 0; count < INPUT_BATCH_SIZE && !quit_requested; ++count)
        if (!process_input())
            return;
}

int main(void)
{
    sound_available = snd_init() != 0;
    sound_enabled = sound_available;
    initialize_game();
    draw_start_screen();
    play_sound(1, SND_FANFARE);

    timer_start();
    while (!quit_requested && state != GAME_OVER && state != GAME_WON) {
        process_input_batch();
        if (!quit_requested && !help_visible && timer_expired()) {
            timer_start();
            move_ball();
            console_flush();
        }
    }

    clear_message();
    if (state == GAME_WON) {
        play_sound(4, SND_FANFARE);
        show_message("YOU CLEARED THE WALL");
    }
    else if (state == GAME_OVER)
        show_message("GAME OVER");
    else if (sound_available)
        snd_stop();
    cursor_move(25, 20);
    set_color(37);
    write_text("FINAL SCORE: ");
    write_unsigned(score);
    reset_color();
    cursor_move(SCREEN_HEIGHT, 1);
    write_text("\033[?25h\r\n");
    console_flush();
    return 0;
}