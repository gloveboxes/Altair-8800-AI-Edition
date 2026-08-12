#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Tetris for CP/M 2.2 on the Z80.
 *
 * dcc C11 implementation with a 7-bag piece supply, simple wall kicks,
 * scoring, levels, soft/hard drop, and VT100/xterm.js output. Console output
 * is accumulated in a static buffer and flushed once per visible update.
 */

#define TIMER_HIGH_PORT 28
#define TIMER_LOW_PORT 29
#define RANDOM_COMMAND_PORT 45
#define RANDOM_DATA_PORT 200
#define FRAME_MILLISECONDS 25

#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20
#define BOARD_ROW 4
#define BOARD_COLUMN 30
#define NEXT_ROW 6
#define NEXT_COLUMN 54

#define ESCAPE_KEY 27
#define CTRL_UP_KEY 5
#define CTRL_DOWN_KEY 24
#define CTRL_LEFT_KEY 19
#define CTRL_RIGHT_KEY 4
#define SPACE_KEY 32

#define CONSOLE_BUFFER_SIZE 4096
#define BAG_SIZE 7
#define ROTATION_COUNT 4

extern int bdos(int function, int value);
extern int inp(unsigned port);
extern void outp(unsigned port, unsigned value);

enum piece_type {
    PIECE_NONE,
    PIECE_I,
    PIECE_O,
    PIECE_T,
    PIECE_S,
    PIECE_Z,
    PIECE_J,
    PIECE_L
};

enum game_state {
    GAME_RUNNING,
    GAME_OVER,
    GAME_QUIT
};

enum key_action {
    KEY_ACTION_NONE,
    KEY_ACTION_QUIT,
    KEY_ACTION_LEFT,
    KEY_ACTION_RIGHT,
    KEY_ACTION_ROTATE,
    KEY_ACTION_SOFT_DROP,
    KEY_ACTION_HARD_DROP
};

enum escape_state {
    ESCAPE_STATE_NONE,
    ESCAPE_STATE_SEEN,
    ESCAPE_STATE_BRACKET_SEEN
};

static const uint16_t piece_masks[8][ROTATION_COUNT] = {
    {0x0000, 0x0000, 0x0000, 0x0000},
    {0x0f00, 0x2222, 0x00f0, 0x4444},
    {0x0660, 0x0660, 0x0660, 0x0660},
    {0x0e40, 0x4c40, 0x4e00, 0x4640},
    {0x06c0, 0x4620, 0x06c0, 0x4620},
    {0x0c60, 0x2640, 0x0c60, 0x2640},
    {0x08e0, 0x6440, 0x0e20, 0x44c0},
    {0x02e0, 0x4460, 0x0e80, 0x0c44}
};

static char console_buffer[CONSOLE_BUFFER_SIZE];
static uint8_t board[BOARD_HEIGHT][BOARD_WIDTH];
static uint8_t piece_bag[BAG_SIZE];
static uint8_t pieces_remaining;

static enum piece_type active_piece;
static uint8_t active_rotation;
static int active_column;
static int active_row;
static enum piece_type next_piece;

static enum game_state game_state;
static long score;
static unsigned lines_cleared;
static uint8_t level;
static uint8_t gravity_tick;
static uint8_t gravity_delay;

static enum piece_type previous_piece;
static uint8_t previous_rotation;
static int previous_column;
static int previous_row;
static bool previous_piece_visible;

static enum escape_state escape_state;
static uint8_t escape_wait_ticks;

static void write_text(const char *text)
{
    while (*text)
        putchar(*text++);
}

static void write_unsigned(unsigned value)
{
    char digits[6];
    uint8_t count = 0;

    if (value == 0) {
        putchar('0');
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count != 0)
        putchar(digits[--count]);
}

static void write_score(long value)
{
    char digits[11];
    uint8_t count = 0;

    if (value == 0) {
        putchar('0');
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count != 0)
        putchar(digits[--count]);
}

static void move_cursor(uint8_t row, uint8_t column)
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
    write_text("\033[0m\033[2J\033[H");
}

static void hide_cursor(void)
{
    write_text("\033[?25l");
}

static void show_cursor(void)
{
    write_text("\033[?25h");
}

static void flush_console(void)
{
    fflush(stdout);
}

static int read_key(void)
{
    return bdos(6, 0xff) & 0xff;
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

static uint8_t piece_background(enum piece_type piece)
{
    switch (piece) {
    case PIECE_I: return 46;
    case PIECE_O: return 43;
    case PIECE_T: return 45;
    case PIECE_S: return 42;
    case PIECE_Z: return 41;
    case PIECE_J: return 44;
    case PIECE_L: return 103;
    default: return 47;
    }
}

static uint8_t cabinet_background(uint8_t row, uint8_t column)
{
    return (((row / 2) + (column / 2)) & 1) != 0 ? 100 : 107;
}

static bool piece_has_cell(enum piece_type piece, uint8_t rotation,
                           uint8_t row, uint8_t column)
{
    uint8_t bit;

    if (piece < PIECE_I || piece > PIECE_L)
        return false;
    bit = (uint8_t)(row * 4 + column);
    return ((piece_masks[piece][rotation & 3] >> (15 - bit)) & 1) != 0;
}

static void refill_piece_bag(void)
{
    int index;

    for (index = 0; index < BAG_SIZE; ++index)
        piece_bag[index] = (uint8_t)(index + 1);

    for (index = BAG_SIZE - 1; index > 0; --index) {
        uint8_t other = (uint8_t)(random_word() % (index + 1));
        uint8_t temporary = piece_bag[index];
        piece_bag[index] = piece_bag[other];
        piece_bag[other] = temporary;
    }
    pieces_remaining = BAG_SIZE;
}

static enum piece_type take_next_piece(void)
{
    if (pieces_remaining == 0)
        refill_piece_bag();
    return (enum piece_type)piece_bag[--pieces_remaining];
}

static bool piece_fits(enum piece_type piece, uint8_t rotation,
                       int column, int row)
{
    uint8_t piece_row;
    uint8_t piece_column;

    for (piece_row = 0; piece_row < 4; ++piece_row) {
        for (piece_column = 0; piece_column < 4; ++piece_column) {
            int board_row;
            int board_column;

            if (!piece_has_cell(piece, rotation, piece_row, piece_column))
                continue;
            board_row = row + piece_row;
            board_column = column + piece_column;
            if (board_column < 0 || board_column >= BOARD_WIDTH ||
                board_row >= BOARD_HEIGHT)
                return false;
            if (board_row >= 0 && board[board_row][board_column] != PIECE_NONE)
                return false;
        }
    }
    return true;
}

static void draw_tile(enum piece_type piece)
{
    if (piece == PIECE_NONE)
        reset_color();
    else
        set_color(piece_background(piece));
    write_text("  ");
}

static void draw_board(void)
{
    uint8_t row;
    uint8_t column;

    for (row = 0; row < BOARD_HEIGHT; ++row) {
        move_cursor((uint8_t)(BOARD_ROW + row + 1), BOARD_COLUMN);
        for (column = 0; column < BOARD_WIDTH; ++column)
            draw_tile((enum piece_type)board[row][column]);
    }
    reset_color();
    previous_piece_visible = false;
}

static void draw_piece(enum piece_type piece, uint8_t rotation,
                       int column, int row)
{
    uint8_t piece_row;
    uint8_t piece_column;

    set_color(piece_background(piece));
    for (piece_row = 0; piece_row < 4; ++piece_row) {
        int board_row = row + piece_row;
        if (board_row < 0 || board_row >= BOARD_HEIGHT)
            continue;
        for (piece_column = 0; piece_column < 4; ++piece_column) {
            int board_column = column + piece_column;
            if (piece_has_cell(piece, rotation, piece_row, piece_column) &&
                board_column >= 0 && board_column < BOARD_WIDTH) {
                move_cursor((uint8_t)(BOARD_ROW + board_row + 1),
                            (uint8_t)(BOARD_COLUMN + board_column * 2));
                write_text("  ");
            }
        }
    }
    reset_color();
}

static void erase_piece(enum piece_type piece, uint8_t rotation,
                        int column, int row)
{
    uint8_t piece_row;
    uint8_t piece_column;

    for (piece_row = 0; piece_row < 4; ++piece_row) {
        int board_row = row + piece_row;
        if (board_row < 0 || board_row >= BOARD_HEIGHT)
            continue;
        for (piece_column = 0; piece_column < 4; ++piece_column) {
            int board_column = column + piece_column;
            if (piece_has_cell(piece, rotation, piece_row, piece_column) &&
                board_column >= 0 && board_column < BOARD_WIDTH) {
                move_cursor((uint8_t)(BOARD_ROW + board_row + 1),
                            (uint8_t)(BOARD_COLUMN + board_column * 2));
                draw_tile((enum piece_type)board[board_row][board_column]);
            }
        }
    }
    reset_color();
}

static void draw_active_piece(void)
{
    if (previous_piece_visible)
        erase_piece(previous_piece, previous_rotation,
                    previous_column, previous_row);

    if (game_state == GAME_RUNNING && active_piece != PIECE_NONE) {
        draw_piece(active_piece, active_rotation, active_column, active_row);
        previous_piece = active_piece;
        previous_rotation = active_rotation;
        previous_column = active_column;
        previous_row = active_row;
        previous_piece_visible = true;
    } else {
        previous_piece_visible = false;
    }
    move_cursor(5, 1);
}

static void draw_title(void)
{
    move_cursor(1, 1);
    set_color(36);
    write_text("TETRIS");
    reset_color();
    write_text(" for ");
    set_color(33);
    write_text("Altair 8800");
    reset_color();
    write_text(" C11");

    move_cursor(2, 1);
    set_color(37);
    write_text("LEFT/RIGHT Move  UP Rotate  DOWN Soft Drop  SPACE Drop  ESC/Q Quit");
    reset_color();
}

static void draw_border(void)
{
    uint8_t row;
    uint8_t column;

    for (row = 0; row < BOARD_HEIGHT + 2; ++row) {
        move_cursor((uint8_t)(BOARD_ROW + row), BOARD_COLUMN - 2);
        for (column = 0; column < BOARD_WIDTH + 2; ++column) {
            set_color(cabinet_background(row, column));
            write_text("  ");
        }
    }
    reset_color();
}

static void draw_statistics(void)
{
    move_cursor(4, 1);
    set_color(35);
    write_text("STATUS");
    reset_color();

    move_cursor(5, 1);
    set_color(36);
    write_text("Score: ");
    set_color(33);
    write_score(score);
    reset_color();
    write_text("          ");

    move_cursor(6, 1);
    set_color(36);
    write_text("Lines: ");
    set_color(33);
    write_unsigned(lines_cleared);
    reset_color();
    write_text("          ");

    move_cursor(7, 1);
    set_color(36);
    write_text("Level: ");
    set_color(33);
    write_unsigned(level);
    reset_color();
    write_text("          ");
}

static void draw_next_piece(void)
{
    uint8_t row;
    uint8_t column;

    move_cursor(NEXT_ROW, NEXT_COLUMN);
    set_color(36);
    write_text("NEXT");
    reset_color();

    for (row = 0; row < 4; ++row) {
        move_cursor((uint8_t)(NEXT_ROW + row + 1), NEXT_COLUMN);
        write_text("        ");
    }
    if (next_piece < PIECE_I || next_piece > PIECE_L)
        return;

    set_color(piece_background(next_piece));
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column) {
            if (piece_has_cell(next_piece, 0, row, column)) {
                move_cursor((uint8_t)(NEXT_ROW + row + 1),
                            (uint8_t)(NEXT_COLUMN + column * 2));
                write_text("  ");
            }
        }
    }
    reset_color();
}

static void update_gravity_delay(void)
{
    gravity_delay = (uint8_t)(20 - level);
    if (gravity_delay < 5)
        gravity_delay = 5;
}

static void add_line_score(uint8_t count)
{
    static const unsigned points[5] = {0, 40, 100, 300, 1200};
    score += (long)points[count] * (level + 1);
}

static uint8_t clear_full_lines(void)
{
    int row;
    uint8_t cleared = 0;

    for (row = BOARD_HEIGHT - 1; row >= 0; --row) {
        uint8_t column;
        bool full = true;

        for (column = 0; column < BOARD_WIDTH; ++column) {
            if (board[row][column] == PIECE_NONE) {
                full = false;
                break;
            }
        }
        if (full) {
            int source_row;
            ++cleared;
            for (source_row = row; source_row > 0; --source_row) {
                for (column = 0; column < BOARD_WIDTH; ++column)
                    board[source_row][column] = board[source_row - 1][column];
            }
            for (column = 0; column < BOARD_WIDTH; ++column)
                board[0][column] = PIECE_NONE;
            ++row;
        }
    }

    if (cleared != 0) {
        lines_cleared += cleared;
        add_line_score(cleared);
        level = (uint8_t)(lines_cleared / 10);
        if (level > 15)
            level = 15;
        update_gravity_delay();
        draw_board();
        draw_statistics();
    }
    return cleared;
}

static void merge_active_piece(void)
{
    uint8_t piece_row;
    uint8_t piece_column;

    for (piece_row = 0; piece_row < 4; ++piece_row) {
        for (piece_column = 0; piece_column < 4; ++piece_column) {
            int board_row;
            int board_column;

            if (!piece_has_cell(active_piece, active_rotation,
                                piece_row, piece_column))
                continue;
            board_row = active_row + piece_row;
            board_column = active_column + piece_column;
            if (board_row >= 0 && board_row < BOARD_HEIGHT &&
                board_column >= 0 && board_column < BOARD_WIDTH)
                board[board_row][board_column] = (uint8_t)active_piece;
        }
    }
    active_piece = PIECE_NONE;
}

static bool spawn_piece(void)
{
    active_piece = next_piece;
    active_rotation = 0;
    active_column = 3;
    active_row = -1;
    gravity_tick = 0;

    next_piece = take_next_piece();
    draw_next_piece();
    if (!piece_fits(active_piece, active_rotation, active_column, active_row)) {
        game_state = GAME_OVER;
        return false;
    }
    draw_active_piece();
    draw_statistics();
    return true;
}

static void lock_piece(void)
{
    merge_active_piece();
    draw_active_piece();
    clear_full_lines();
    if (game_state == GAME_RUNNING)
        spawn_piece();
}

static bool try_move(int column, int row, uint8_t rotation)
{
    if (!piece_fits(active_piece, rotation, column, row))
        return false;
    active_column = column;
    active_row = row;
    active_rotation = rotation & 3;
    gravity_tick = 0;
    draw_active_piece();
    return true;
}

static bool rotate_piece(void)
{
    uint8_t rotation = (uint8_t)((active_rotation + 1) & 3);

    if (try_move(active_column, active_row, rotation)) return true;
    if (try_move(active_column - 1, active_row, rotation)) return true;
    if (try_move(active_column + 1, active_row, rotation)) return true;
    if (try_move(active_column - 2, active_row, rotation)) return true;
    return try_move(active_column + 2, active_row, rotation);
}

static void soft_drop(void)
{
    if (try_move(active_column, active_row + 1, active_rotation)) {
        ++score;
        draw_statistics();
    } else {
        lock_piece();
    }
}

static void hard_drop(void)
{
    while (piece_fits(active_piece, active_rotation,
                      active_column, active_row + 1)) {
        ++active_row;
        score += 2;
    }
    gravity_tick = 0;
    draw_active_piece();
    draw_statistics();
    lock_piece();
}

static void advance_gravity(void)
{
    if (++gravity_tick < gravity_delay)
        return;
    gravity_tick = 0;
    if (piece_fits(active_piece, active_rotation,
                   active_column, active_row + 1)) {
        ++active_row;
        draw_active_piece();
    } else {
        lock_piece();
    }
}

static enum key_action decode_key(int character)
{
    if (escape_state == ESCAPE_STATE_BRACKET_SEEN) {
        escape_state = ESCAPE_STATE_NONE;
        if (character == 'A') return KEY_ACTION_ROTATE;
        if (character == 'B') return KEY_ACTION_SOFT_DROP;
        if (character == 'C') return KEY_ACTION_RIGHT;
        if (character == 'D') return KEY_ACTION_LEFT;
        return KEY_ACTION_NONE;
    }
    if (escape_state == ESCAPE_STATE_SEEN) {
        escape_state = ESCAPE_STATE_NONE;
        if (character == '[') {
            escape_state = ESCAPE_STATE_BRACKET_SEEN;
            return KEY_ACTION_NONE;
        }
        return KEY_ACTION_QUIT;
    }
    if (character == ESCAPE_KEY) {
        escape_state = ESCAPE_STATE_SEEN;
        escape_wait_ticks = 0;
        return KEY_ACTION_NONE;
    }
    if (character == 'q' || character == 'Q') return KEY_ACTION_QUIT;
    if (character == CTRL_LEFT_KEY) return KEY_ACTION_LEFT;
    if (character == CTRL_RIGHT_KEY) return KEY_ACTION_RIGHT;
    if (character == CTRL_UP_KEY) return KEY_ACTION_ROTATE;
    if (character == CTRL_DOWN_KEY) return KEY_ACTION_SOFT_DROP;
    if (character == SPACE_KEY) return KEY_ACTION_HARD_DROP;
    return KEY_ACTION_NONE;
}

static void apply_action(enum key_action action)
{
    if (action == KEY_ACTION_QUIT) {
        game_state = GAME_QUIT;
        return;
    }
    if (game_state != GAME_RUNNING)
        return;

    switch (action) {
    case KEY_ACTION_LEFT:
        try_move(active_column - 1, active_row, active_rotation);
        break;
    case KEY_ACTION_RIGHT:
        try_move(active_column + 1, active_row, active_rotation);
        break;
    case KEY_ACTION_ROTATE:
        rotate_piece();
        break;
    case KEY_ACTION_SOFT_DROP:
        soft_drop();
        break;
    case KEY_ACTION_HARD_DROP:
        hard_drop();
        break;
    default:
        break;
    }
}

static void update_escape_timeout(void)
{
    if (escape_state == ESCAPE_STATE_SEEN && ++escape_wait_ticks > 2) {
        escape_state = ESCAPE_STATE_NONE;
        game_state = GAME_QUIT;
    }
}

static void draw_final_message(void)
{
    move_cursor(15, 5);
    set_color(game_state == GAME_QUIT ? 36 : 35);
    write_text(game_state == GAME_QUIT ? "GAME QUIT " : "GAME OVER!");
    reset_color();

    move_cursor(16, 5);
    set_color(33);
    write_text("Final Score: ");
    reset_color();
    write_score(score);

    move_cursor(17, 5);
    set_color(33);
    write_text("Lines Cleared: ");
    reset_color();
    write_unsigned(lines_cleared);

    move_cursor(18, 5);
    set_color(33);
    write_text("Level Reached: ");
    reset_color();
    write_unsigned(level);
}

static void initialize_game(void)
{
    uint8_t row;
    uint8_t column;

    for (row = 0; row < BOARD_HEIGHT; ++row) {
        for (column = 0; column < BOARD_WIDTH; ++column)
            board[row][column] = PIECE_NONE;
    }
    pieces_remaining = 0;
    active_piece = PIECE_NONE;
    next_piece = take_next_piece();
    game_state = GAME_RUNNING;
    score = 0;
    lines_cleared = 0;
    level = 0;
    gravity_tick = 0;
    update_gravity_delay();
    previous_piece_visible = false;
    escape_state = ESCAPE_STATE_NONE;
    escape_wait_ticks = 0;
}

int main(void)
{
    setvbuf(stdout, console_buffer, _IOFBF, sizeof(console_buffer));
    initialize_game();

    clear_screen();
    hide_cursor();
    draw_title();
    draw_border();
    draw_board();
    draw_statistics();
    draw_next_piece();
    spawn_piece();
    flush_console();

    while (game_state == GAME_RUNNING) {
        start_frame_timer();
        advance_gravity();
        flush_console();

        while (frame_timer_active() && game_state == GAME_RUNNING) {
            int character = read_key();
            if (character != 0) {
                enum key_action action = decode_key(character);
                if (action != KEY_ACTION_NONE) {
                    apply_action(action);
                    flush_console();
                }
            }
        }
        update_escape_timeout();
    }

    draw_active_piece();
    draw_statistics();
    draw_final_message();
    move_cursor(24, 1);
    show_cursor();
    write_text("Thanks for playing Tetris!\r\n");
    flush_console();
    return 0;
}
