/* Breakout for dcc C11, CP/M 2.2, and the Altair 8800 emulator. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ESCAPE_KEY 27
#define CTRL_C_KEY 3
#define SPACE_KEY 32
#define ENTER_KEY 13
#define RIGHT_KEY 4
#define LEFT_KEY 19

#define TIMER_HIGH_PORT 26
#define TIMER_LOW_PORT 27
#define RANDOM_COMMAND_PORT 45
#define RESPONSE_PORT 200
#define INITIAL_BALL_DELAY_MS 80
#define MEDIUM_BALL_DELAY_MS 70
#define FAST_BALL_DELAY_MS 60

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
#define PADDLE_STEP 5
#define CONSOLE_BUFFER_SIZE 4096
#define NO_BRICK 0xff

enum game_state {
    WAITING_TO_LAUNCH,
    PLAYING,
    GAME_OVER,
    GAME_WON
};

extern int inp(unsigned port);
extern void outp(unsigned port, unsigned value);
extern int bdos(int function, int argument);

static char console_buffer[CONSOLE_BUFFER_SIZE];
static uint8_t bricks[BRICK_ROWS][BRICK_COLUMNS];
static uint8_t brick_column_at[SCREEN_WIDTH + 1];

static int ball_column;
static int ball_row;
static int ball_dx;
static int ball_dy;
static int paddle_column;
static unsigned score;
static uint8_t lives;
static uint8_t bricks_left;
static enum game_state state;
static uint8_t ball_delay_ms;
static bool quit_requested;

static void write_text(const char *text)
{
    while (*text != '\0')
        putchar(*text++);
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

static void cursor_move(uint8_t row, uint8_t column)
{
    putchar(ESCAPE_KEY);
    putchar('[');
    write_unsigned(row);
    putchar(';');
    write_unsigned(column);
    putchar('H');
}

static void set_color(uint8_t color)
{
    putchar(ESCAPE_KEY);
    putchar('[');
    write_unsigned(color);
    putchar('m');
}

static void reset_color(void)
{
    write_text("\033[0m");
}

static void clear_screen(void)
{
    write_text("\033[0m\033[2J\033[H\033[?25l");
}

static void erase_to_end_of_line(void)
{
    write_text("\033[K");
}

static void timer_start(void)
{
    outp(TIMER_HIGH_PORT, 0);
    outp(TIMER_LOW_PORT, ball_delay_ms);
}

static bool timer_expired(void)
{
    return inp(TIMER_LOW_PORT) == 0;
}

static unsigned random_word(void)
{
    unsigned low;

    outp(RANDOM_COMMAND_PORT, 1);
    low = (unsigned)inp(RESPONSE_PORT);
    return low | ((unsigned)inp(RESPONSE_PORT) << 8);
}

static int read_key(void)
{
    return bdos(6, 0xff) & 0xff;
}

static void draw_status(void)
{
    set_color(37);
    cursor_move(1, 2);
    write_text("SCORE:");
    write_unsigned(score);
    erase_to_end_of_line();
    cursor_move(1, 50);
    write_text("LIFE:");
    write_unsigned(lives);
    reset_color();
}

static uint8_t checker_color(uint8_t row, uint8_t column)
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

static uint8_t brick_color(uint8_t row)
{
    static const uint8_t colors[BRICK_ROWS] = {105, 101, 43, 103, 102, 104, 45, 106};
    return colors[row];
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
        putchar(' ');
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
        putchar(' ');
    reset_color();
}

static void erase_paddle(void)
{
    uint8_t width;

    cursor_move(PADDLE_ROW, paddle_column);
    for (width = 0; width < PADDLE_WIDTH; width++)
        putchar(' ');
}

static void draw_ball(void)
{
    set_color(97);
    cursor_move(ball_row, ball_column);
    putchar('O');
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
    putchar(' ');
}

static void show_message(const char *message)
{
    uint8_t column;

    reset_color();
    cursor_move(23, MINIMUM_COLUMN);
    for (column = MINIMUM_COLUMN; column <= MAXIMUM_COLUMN; column++)
        putchar(' ');
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
        putchar(' ');
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
    quit_requested = false;
}

static void serve_ball(void)
{
    paddle_column = (SCREEN_WIDTH - PADDLE_WIDTH) >> 1;
    ball_column = paddle_column + (PADDLE_WIDTH >> 1);
    ball_row = PADDLE_ROW - 1;
    ball_dx = (random_word() & 1) ? -1 : 1;
    ball_dy = -1;
    state = WAITING_TO_LAUNCH;
    draw_paddle();
    draw_ball();
    show_message("SPACE TO LAUNCH");
}

static bool paddle_hit(void)
{
    int relative_column;

    if (ball_dy <= 0 || ball_row != PADDLE_ROW - 1 ||
        ball_column < paddle_column || ball_column >= paddle_column + PADDLE_WIDTH)
        return false;
    relative_column = ball_column - paddle_column;
    ball_dy = -1;
    if (relative_column < 2)
        ball_dx = -1;
    else if (relative_column > PADDLE_WIDTH - 3)
        ball_dx = 1;
    score++;
    draw_status();
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
    score += (BRICK_ROWS - row) * 10;
    if (bricks_left < 12)
        ball_delay_ms = FAST_BALL_DELAY_MS;
    else if (bricks_left < 45)
        ball_delay_ms = MEDIUM_BALL_DELAY_MS;
    draw_brick((uint8_t)row, column);
    draw_status();
    if (bricks_left == 0)
        state = GAME_WON;
    return true;
}

static void resolve_brick_collision(uint8_t previous_row, uint8_t previous_column)
{
    bool vertical_hit;
    bool horizontal_hit;

    if (hit_brick((uint8_t)ball_row, (uint8_t)ball_column)) {
        if (previous_row == ball_row)
            ball_dx = -ball_dx;
        else
            ball_dy = -ball_dy;
        return;
    }
    if (previous_row == ball_row || previous_column == ball_column)
        return;
    vertical_hit = hit_brick((uint8_t)ball_row, previous_column);
    horizontal_hit = hit_brick(previous_row, (uint8_t)ball_column);
    if (vertical_hit)
        ball_dy = -ball_dy;
    if (horizontal_hit)
        ball_dx = -ball_dx;
}

static void lose_ball(void)
{
    lives--;
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

    if (state != PLAYING)
        return;
    previous_column = (uint8_t)ball_column;
    previous_row = (uint8_t)ball_row;
    ball_column += ball_dx;
    ball_row += ball_dy;
    if (ball_column <= MINIMUM_COLUMN) {
        ball_column = MINIMUM_COLUMN;
        ball_dx = 1;
    } else if (ball_column >= MAXIMUM_COLUMN) {
        ball_column = MAXIMUM_COLUMN;
        ball_dx = -1;
    }
    if (ball_row <= MINIMUM_ROW) {
        ball_row = MINIMUM_ROW;
        ball_dy = 1;
    }
    paddle_hit();
    if (ball_row >= MAXIMUM_ROW) {
        restore_cell(previous_row, previous_column);
        lose_ball();
        return;
    }
    resolve_brick_collision(previous_row, previous_column);
    restore_cell(previous_row, previous_column);
    draw_ball();
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
        draw_ball();
    }
}

static void process_input(void)
{
    int key = read_key();

    if (key == 0)
        return;
    if (key == ESCAPE_KEY || key == CTRL_C_KEY) {
        quit_requested = true;
        return;
    }
    if (key == LEFT_KEY)
        move_paddle(-PADDLE_STEP);
    else if (key == RIGHT_KEY)
        move_paddle(PADDLE_STEP);
    else if ((key == SPACE_KEY || key == ENTER_KEY) && state == WAITING_TO_LAUNCH) {
        clear_message();
        state = PLAYING;
        move_ball();
        fflush(stdout);
        timer_start();
    }
}

int main(void)
{
    setvbuf(stdout, console_buffer, _IOFBF, sizeof(console_buffer));
    initialize_game();
    clear_screen();
    draw_status();
    draw_border();
    draw_all_bricks();
    serve_ball();
    fflush(stdout);

    timer_start();
    while (!quit_requested && state != GAME_OVER && state != GAME_WON) {
        process_input();
        if (timer_expired()) {
            move_ball();
            fflush(stdout);
            timer_start();
        }
    }

    clear_message();
    if (state == GAME_WON)
        show_message("YOU CLEARED THE WALL");
    else if (state == GAME_OVER)
        show_message("GAME OVER");
    cursor_move(25, 20);
    set_color(37);
    write_text("FINAL SCORE: ");
    write_unsigned(score);
    reset_color();
    cursor_move(SCREEN_HEIGHT, 1);
    write_text("\033[?25h\r\n");
    fflush(stdout);
    return 0;
}