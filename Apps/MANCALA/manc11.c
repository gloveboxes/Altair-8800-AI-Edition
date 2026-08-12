#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * MANC11.C - Mancala / African Bean Game for CP/M 2.2 on the Z80.
 *
 * dcc C11 port of the BDS C MANCALA.C app.  VT100/xterm.js display,
 * 30 rows by 80 columns, player vs computer, Kalah-style rules with an
 * alpha-beta search engine (Easy/Medium/Hard).
 *
 * Console output is fully buffered through stdout.  A single 8 KB buffer
 * is installed once at startup with setvbuf(); fflush() drains it at the
 * points where the player needs to see the board (before input and during
 * the computer's "thinking" pause).  This keeps the many small VT100 writes
 * fast instead of issuing one BDOS call per character.
 *
 * Build from the dcc repo with this file as manc11.c (bump the C stack so
 * the search recursion has room):
 *   DCC_STACK_SIZE=8192 ./ma.sh manc11 peep   -> MANC11.COM
 *   ntvcm MANC11
 *
 * Keys:
 *   E/M/H              Easy / Medium / Hard
 *   Left/Right arrows  Select pit
 *   1-6                Select pit
 *   Space or Return    Sow selected pit
 *   U                  Undo last player move
 *   ? or T             Hint
 *   Q, ESC, Ctrl-C     Quit
 */

#define PITS_PER_SIDE 6
#define TOTAL_PITS 14
#define HUMAN_STORE 6
#define COMPUTER_STORE 13

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 30

#define BOARD_ROW 7
#define BOARD_COLUMN 18
#define PIT_CELL_WIDTH 7
#define PIT_CELL_HEIGHT 4
#define PANEL_TOP 5
#define PANEL_LEFT 5
#define PANEL_WIDTH 72
#define PANEL_HEIGHT 21

#define ESCAPE_KEY 27
#define LEFT_ARROW_KEY 19
#define RIGHT_ARROW_KEY 4
#define SPACE_KEY 32
#define CTRL_C_KEY 3
#define ENTER_KEY 13

#define CONSOLE_BUFFER_SIZE 8192

enum key_action {
    KEY_ACTION_NONE,
    KEY_ACTION_LEFT,
    KEY_ACTION_RIGHT,
    KEY_ACTION_SOW,
    KEY_ACTION_QUIT
};

enum difficulty_level {
    LEVEL_EASY,
    LEVEL_MEDIUM,
    LEVEL_HARD
};

enum player {
    PLAYER_HUMAN,
    PLAYER_COMPUTER
};

/* Outcome of sowing a move on the real or a simulated board.  SOW_GAME_ENDED
 * also covers the defensive invalid-move guard (never hit by real callers,
 * which only sow non-empty owned pits). */
enum sow_result {
    SOW_GAME_ENDED,
    SOW_TURN_PASSES,
    SOW_EXTRA_TURN
};

/* VT100 arrows arrive as raw control codes or as an ESC '[' letter sequence
 * depending on the front end; this tracks progress through the latter. */
enum escape_state {
    ESCAPE_STATE_NONE,
    ESCAPE_STATE_ESC_SEEN,
    ESCAPE_STATE_BRACKET_SEEN
};

extern int bdos(int func, int val);
extern int inp(unsigned port);
extern void outp(unsigned port, unsigned val);

/* Single console buffer, installed once at startup. */
static char console_buffer[CONSOLE_BUFFER_SIZE];

static uint8_t board[TOTAL_PITS];
static uint8_t undo_board[TOTAL_PITS];
static int selected;
static int undo_selected;
static bool quit_requested;
static bool game_over;
static enum escape_state escape_state;
static enum difficulty_level difficulty;
static bool undo_available;

/* --- low-level console helpers -------------------------------------- */

/* Drain the buffered console output. */
static void flush(void)
{
    fflush(stdout);
}

static void write_text(const char *text)
{
    while (*text)
        putchar(*text++);
}

/* Print a signed integer in decimal. */
static void write_number(int n)
{
    char digits[6];
    uint8_t count;

    if (n == 0) {
        putchar('0');
        return;
    }
    if (n < 0) {
        putchar('-');
        n = -n;
    }
    count = 0;
    while (n > 0 && count < 6) {
        digits[count++] = (char)((n % 10) + '0');
        n /= 10;
    }
    while (count--)
        putchar(digits[count]);
}

/* Print a right-aligned two-column number. */
static void write_padded_number(int n)
{
    if (n < 10)
        putchar(' ');
    write_number(n);
}

/* Move the cursor to a 1-based row/column. */
static void cursor_move(uint8_t row, uint8_t column)
{
    putchar(ESCAPE_KEY);
    putchar('[');
    write_number(row);
    putchar(';');
    write_number(column);
    putchar('H');
}

/* Emit ESC[Nm for any SGR code (fg, bg, attr). */
static void set_color(uint8_t color)
{
    putchar(ESCAPE_KEY);
    putchar('[');
    write_number(color);
    putchar('m');
}

static void reset_color(void)
{
    write_text("\033[0m");
}

static void clear_screen(void)
{
    write_text("\033[2J\033[0m");
    cursor_move(1, 1);
}

static void hide_cursor(void)
{
    write_text("\033[?25l");
}

static void show_cursor(void)
{
    write_text("\033[?25h");
}

static void erase_to_end_of_line(void)
{
    write_text("\033[K");
}

/* Flush the screen, then read one key without waiting. */
static int read_key(void)
{
    flush();
    return bdos(6, 0xFF) & 0xFF;
}

/* Read a 16-bit random number from the emulator RNG port. */
static uint16_t random_word(void)
{
    uint16_t value;

    outp(45, 1);
    value = (uint16_t)inp(200);
    value |= (uint16_t)(inp(200) << 8);
    return value;
}

/* --- forward declarations -------------------------------------------- */

static void show_hint(void);
static void redraw(int i);
static void draw_pit(int i, bool highlighted);
static void update_pit(int i);
static void update_store(int i);
static bool board_has_move(const uint8_t b[], enum player p);
static void board_collect_endgame(uint8_t b[]);
static int alpha_beta(const uint8_t b[], enum player p, int depth, int alpha,
                      int beta, bool order);
static int move_order_score(const uint8_t b[], int i, enum player p);
static int order_moves(const uint8_t b[], enum player p, int mv[]);

/* --- board drawing --------------------------------------------------- */

/* Select the board's dark wood color. */
static void wood_color(void)
{
    set_color(44);
    set_color(97);
}

/* Select recessed pit color. */
static void hole_color(void)
{
    set_color(41);
    set_color(97);
}

/* African-inspired checker border color. */
static inline uint8_t checker_color(uint8_t row, uint8_t column)
{
    if (((row / 2) + (column / 2)) & 1) {
        if (row & 1)
            return 42;
        return 43;
    }
    if (column & 1)
        return 41;
    return 44;
}

/* Draw the checker outline. */
static void draw_border(void)
{
    for (uint8_t column = 0; column < SCREEN_WIDTH; column += 2) {
        set_color(checker_color(0, column / 2));
        cursor_move(2, column + 1);
        write_text("  ");
        set_color(checker_color(SCREEN_HEIGHT - 1, column / 2));
        cursor_move(SCREEN_HEIGHT, column + 1);
        write_text("  ");
    }
    for (uint8_t row = 3; row < SCREEN_HEIGHT; row++) {
        set_color(checker_color(row - 2, 0));
        cursor_move(row, 1);
        write_text("  ");
        set_color(checker_color(row - 2, (SCREEN_WIDTH / 2) - 1));
        cursor_move(row, SCREEN_WIDTH - 1);
        write_text("  ");
    }
    reset_color();
}

/* Draw score stores and turn. */
static void draw_status(void)
{
    reset_color();
    set_color(37);
    cursor_move(1, 3);
    write_text("MANCALA - THE AFRICAN BEAN GAME");
    cursor_move(1, 49);
    write_text("YOU:");
    write_padded_number(board[HUMAN_STORE]);
    write_text("  CPU:");
    write_padded_number(board[COMPUTER_STORE]);
    write_text("  ");
    if (difficulty == LEVEL_EASY)
        write_text("E:EASY");
    else if (difficulty == LEVEL_MEDIUM)
        write_text("M:MED");
    else
        write_text("H:HARD");
    erase_to_end_of_line();
}

/* Screen column for pit i. */
static inline uint8_t pit_column(int i)
{
    if (i < HUMAN_STORE)
        return BOARD_COLUMN + (i * (PIT_CELL_WIDTH + 1));
    return BOARD_COLUMN + ((12 - i) * (PIT_CELL_WIDTH + 1));
}

/* Screen row for pit i. */
static inline uint8_t pit_row(int i)
{
    if (i < HUMAN_STORE)
        return BOARD_ROW + 10;
    return BOARD_ROW + 1;
}

/* Draw bean dots for a pit. */
static void draw_beans(int n)
{
    int shown = (n > 5) ? 5 : n;
    int i;

    for (i = 0; i < shown; i++)
        putchar('o');
    for (; i < 6; i++)
        putchar(' ');
}

/* Draw one small pit. */
static void draw_pit(int i, bool highlighted)
{
    uint8_t r = pit_row(i);
    uint8_t c = pit_column(i);

    for (uint8_t row = 0; row < PIT_CELL_HEIGHT; row++) {
        cursor_move(r + row, c);
        if (highlighted) {
            set_color(43);
            set_color(30);
        } else {
            hole_color();
        }
        if (row == 0 || row == PIT_CELL_HEIGHT - 1) {
            write_text("       ");
        } else if (row == 1) {
            putchar(' ');
            write_padded_number(board[i]);
            write_text("    ");
        } else {
            putchar(' ');
            if (board[i] > 0)
                draw_beans(board[i]);
            else
                write_text("      ");
        }
        reset_color();
    }
}

/* Update only the changing rows of one small pit. */
static void update_pit(int i)
{
    uint8_t r = pit_row(i);
    uint8_t c = pit_column(i);
    bool highlighted = (i < HUMAN_STORE && i == selected);

    cursor_move(r + 1, c);
    if (highlighted) {
        set_color(43);
        set_color(30);
    } else {
        hole_color();
    }
    putchar(' ');
    write_padded_number(board[i]);
    write_text("    ");

    cursor_move(r + 2, c);
    if (highlighted) {
        set_color(43);
        set_color(30);
    } else {
        hole_color();
    }
    putchar(' ');
    if (board[i] > 0)
        draw_beans(board[i]);
    else
        write_text("      ");
    reset_color();
}

/* Draw one store. */
static void draw_store(int i)
{
    uint8_t c = (i == HUMAN_STORE) ? 67 : 8;
    uint8_t r = BOARD_ROW + 4;

    for (uint8_t row = 0; row < 8; row++) {
        cursor_move(r + row, c);
        hole_color();
        if (row == 0 || row == 7) {
            write_text("       ");
        } else if (row == 2) {
            write_text("  ");
            write_padded_number(board[i]);
            write_text("   ");
        } else if (row == 4) {
            putchar(' ');
            if (i == HUMAN_STORE)
                write_text("YOU ");
            else
                write_text("CPU ");
            write_text("  ");
        } else {
            write_text("       ");
        }
        reset_color();
    }
}

/* Update only the changing count row of one store. */
static void update_store(int i)
{
    uint8_t c = (i == HUMAN_STORE) ? 67 : 8;
    uint8_t r = BOARD_ROW + 4;

    cursor_move(r + 2, c);
    hole_color();
    write_text("  ");
    write_padded_number(board[i]);
    write_text("   ");
    reset_color();
}

/* Clear a message row without touching the border. */
static void clear_note_row(uint8_t row)
{
    reset_color();
    cursor_move(row, 5);
    for (uint8_t column = 0; column < 72; column++)
        putchar(' ');
}

/* Draw the carved wooden board panel. */
static void draw_panel(void)
{
    for (uint8_t row = 0; row < PANEL_HEIGHT; row++) {
        cursor_move(PANEL_TOP + row, PANEL_LEFT);
        wood_color();
        for (uint8_t column = 0; column < PANEL_WIDTH; column++)
            putchar(' ');
        reset_color();
    }
    for (uint8_t row = PANEL_TOP + 3; row < PANEL_TOP + PANEL_HEIGHT - 2; row += 4) {
        cursor_move(row, PANEL_LEFT + 4);
        wood_color();
        set_color(33);
        write_text("................................................................");
        reset_color();
    }
}

/* Draw pit labels. */
static void draw_labels(void)
{
    reset_color();
    wood_color();
    set_color(93);
    cursor_move(6, 30);
    write_text("COMPUTER SIDE");
    reset_color();
    wood_color();
    set_color(93);
    cursor_move(24, 33);
    write_text("YOUR SIDE");
    for (int i = 0; i < PITS_PER_SIDE; i++) {
        wood_color();
        set_color(93);
        cursor_move(BOARD_ROW + 15, pit_column(i) + 3);
        putchar('1' + i);
        reset_color();
    }
    reset_color();
}

/* Draw a status message. */
static void show_message(const char *message)
{
    clear_note_row(27);
    set_color(37);
    cursor_move(27, 5);
    write_text(message);
    reset_color();
    flush();
}

/* Draw the persistent shortcut keys. */
static void draw_help(void)
{
    clear_note_row(28);
    set_color(37);
    cursor_move(28, 5);
    write_text("KEYS: 1-6 SELECT  SPACE SOW  U UNDO  E/M/H LEVEL  ? HINT  Q QUIT");
    reset_color();
}

/* Draw the whole board and pits. */
static void draw_all(void)
{
    clear_screen();
    hide_cursor();
    draw_border();
    draw_status();
    draw_panel();
    draw_labels();
    draw_store(COMPUTER_STORE);
    draw_store(HUMAN_STORE);
    for (int i = 0; i < HUMAN_STORE; i++)
        draw_pit(i, i == selected);
    for (int i = 7; i < COMPUTER_STORE; i++)
        draw_pit(i, false);
    show_message("YOUR TURN: CHOOSE A PIT WITH BEANS");
    draw_help();
}

/* Redraw pit and store positions only. */
static void draw_positions(void)
{
    draw_status();
    draw_store(COMPUTER_STORE);
    draw_store(HUMAN_STORE);
    for (int i = 0; i < HUMAN_STORE; i++)
        draw_pit(i, i == selected);
    for (int i = 7; i < COMPUTER_STORE; i++)
        draw_pit(i, false);
}

/* Redraw a pit or store by index. */
static void redraw(int i)
{
    if (i == HUMAN_STORE || i == COMPUTER_STORE)
        update_store(i);
    else
        update_pit(i);
}

/* --- game state ------------------------------------------------------ */

/* Initialize game state. */
static void initialize_game(void)
{
    for (int i = 0; i < TOTAL_PITS; i++)
        board[i] = 4;
    board[HUMAN_STORE] = 0;
    board[COMPUTER_STORE] = 0;
    selected = 0;
    quit_requested = false;
    game_over = false;
    escape_state = ESCAPE_STATE_NONE;
    difficulty = LEVEL_MEDIUM;
    undo_available = false;
}

/* Decode raw or translated control keys. */
static enum key_action decode_key(int c)
{
    if (escape_state == ESCAPE_STATE_BRACKET_SEEN) {
        if (c == 0)
            return KEY_ACTION_NONE;
        escape_state = ESCAPE_STATE_NONE;
        if (c == 'C')
            return KEY_ACTION_RIGHT;
        if (c == 'D')
            return KEY_ACTION_LEFT;
        return KEY_ACTION_NONE;
    }
    if (escape_state == ESCAPE_STATE_ESC_SEEN) {
        if (c == 0)
            return KEY_ACTION_NONE;
        escape_state = ESCAPE_STATE_NONE;
        if (c == '[') {
            escape_state = ESCAPE_STATE_BRACKET_SEEN;
            return KEY_ACTION_NONE;
        }
        return KEY_ACTION_QUIT;
    }
    if (c == 0)
        return KEY_ACTION_NONE;
    if (c == ESCAPE_KEY) {
        escape_state = ESCAPE_STATE_ESC_SEEN;
        return KEY_ACTION_NONE;
    }
    if (c == CTRL_C_KEY || c == 'Q' || c == 'q')
        return KEY_ACTION_QUIT;
    if (c == LEFT_ARROW_KEY)
        return KEY_ACTION_LEFT;
    if (c == RIGHT_ARROW_KEY)
        return KEY_ACTION_RIGHT;
    if (c == SPACE_KEY || c == ENTER_KEY)
        return KEY_ACTION_SOW;
    return KEY_ACTION_NONE;
}

/* Collect remaining beans at game end. */
static void collect_endgame(void)
{
    board_collect_endgame(board);
    game_over = true;
}

/* Return the opposite pit index. */
static inline int opposite_pit(int i)
{
    return 12 - i;
}

/* Test whether pit i belongs to player p. */
static inline bool owns_pit(int i, enum player p)
{
    if (p == PLAYER_HUMAN && i >= 0 && i < HUMAN_STORE)
        return true;
    if (p == PLAYER_COMPUTER && i > HUMAN_STORE && i < COMPUTER_STORE)
        return true;
    return false;
}

/* Return the store index for player p. */
static inline int store_index(enum player p)
{
    if (p == PLAYER_HUMAN)
        return HUMAN_STORE;
    return COMPUTER_STORE;
}

/* Test whether index i is the opponent's store. */
static inline bool is_opponent_store(int i, enum player p)
{
    if (p == PLAYER_HUMAN && i == COMPUTER_STORE)
        return true;
    if (p == PLAYER_COMPUTER && i == HUMAN_STORE)
        return true;
    return false;
}

/* Sow from pit i for player p (with display). */
static enum sow_result play_move(int i, enum player p)
{
    int cnt;
    int pos;
    int op;
    int st;

    if (!owns_pit(i, p) || board[i] == 0)
        return SOW_GAME_ENDED;
    cnt = board[i];
    board[i] = 0;
    redraw(i);
    pos = i;
    while (cnt > 0) {
        pos++;
        if (pos >= TOTAL_PITS)
            pos = 0;
        if (!is_opponent_store(pos, p)) {
            board[pos]++;
            cnt--;
            redraw(pos);
            if (pos == HUMAN_STORE || pos == COMPUTER_STORE)
                draw_status();
        }
    }

    st = store_index(p);
    if (owns_pit(pos, p) && board[pos] == 1) {
        op = opposite_pit(pos);
        if (board[op] > 0) {
            board[st] = (uint8_t)(board[st] + board[op] + 1);
            board[op] = 0;
            board[pos] = 0;
            redraw(op);
            redraw(pos);
            redraw(st);
            draw_status();
            if (p == PLAYER_HUMAN)
                show_message("CAPTURE! THE OPPOSITE PIT FALLS TO YOU");
            else
                show_message("COMPUTER CAPTURES FROM THE OPPOSITE PIT");
        }
    }

    if (!board_has_move(board, PLAYER_HUMAN) ||
        !board_has_move(board, PLAYER_COMPUTER)) {
        collect_endgame();
        draw_positions();
        return SOW_GAME_ENDED;
    }
    draw_status();
    return (pos == st) ? SOW_EXTRA_TURN : SOW_TURN_PASSES;
}

/* Return true if player p has a legal move. */
static inline bool has_legal_move(enum player p)
{
    return board_has_move(board, p);
}

/* --- simulated board (search) ---------------------------------------- */

/* Copy a simulated board (Z80 LDIR block copy); uint8_t halves the per-node
 * memcpy versus int, and this runs at every search node. */
static void board_copy(uint8_t dst[], const uint8_t src[])
{
    memcpy(dst, src, TOTAL_PITS * sizeof(uint8_t));
}

/* Test for a simulated legal move. */
static bool board_has_move(const uint8_t b[], enum player p)
{
    if (p == PLAYER_HUMAN) {
        for (int i = 0; i < HUMAN_STORE; i++)
            if (b[i] > 0)
                return true;
    } else {
        for (int i = 7; i < COMPUTER_STORE; i++)
            if (b[i] > 0)
                return true;
    }
    return false;
}

/* Sweep simulated end-game beans. */
static void board_collect_endgame(uint8_t b[])
{
    for (int i = 0; i < HUMAN_STORE; i++) {
        b[HUMAN_STORE] += b[i];
        b[i] = 0;
    }
    for (int i = 7; i < COMPUTER_STORE; i++) {
        b[COMPUTER_STORE] += b[i];
        b[i] = 0;
    }
}

/* Sow on a simulated board. */
static enum sow_result board_sow(uint8_t b[], int i, enum player p)
{
    int cnt;
    int pos;
    int op;
    int st;
    int os;

    if (!owns_pit(i, p) || b[i] == 0)
        return SOW_GAME_ENDED;
    /* st/os hoisted out of the per-bean loop below: this runs once per sown
     * bean at every search node, so avoiding a branch on p per bean matters.
     * st = own store, os = the opponent store that sowing must skip. */
    st = store_index(p);
    os = store_index(p == PLAYER_HUMAN ? PLAYER_COMPUTER : PLAYER_HUMAN);
    cnt = b[i];
    b[i] = 0;
    pos = i;
    while (cnt > 0) {
        pos++;
        if (pos >= TOTAL_PITS)
            pos = 0;
        if (pos != os) {
            b[pos]++;
            cnt--;
        }
    }

    /* Capture: last bean lands in an own, previously-empty, non-store pit. */
    if (pos != st && b[pos] == 1 &&
        ((p == PLAYER_HUMAN && pos < HUMAN_STORE) ||
         (p == PLAYER_COMPUTER && pos > HUMAN_STORE && pos < COMPUTER_STORE))) {
        op = 12 - pos;
        if (b[op] > 0) {
            b[st] = (uint8_t)(b[st] + b[op] + 1);
            b[op] = 0;
            b[pos] = 0;
        }
    }
    if (!board_has_move(b, PLAYER_HUMAN) || !board_has_move(b, PLAYER_COMPUTER)) {
        board_collect_endgame(b);
        return SOW_GAME_ENDED;
    }
    return (pos == st) ? SOW_EXTRA_TURN : SOW_TURN_PASSES;
}

/* Return the absolute value. */
static inline int absolute_value(int n)
{
    if (n < 0)
        return -n;
    return n;
}

/* Immediate move-ordering score. */
static int move_order_score(const uint8_t b[], int i, enum player p)
{
    uint8_t tb[TOTAL_PITS];
    int st;
    int old;
    enum sow_result result;
    int gain;
    int dif;
    int val;

    if (!owns_pit(i, p) || b[i] == 0)
        return -30000;
    board_copy(tb, b);
    st = store_index(p);
    old = tb[st];
    result = board_sow(tb, i, p);
    gain = tb[st] - old;
    val = (gain * 100) + b[i];
    if (result == SOW_EXTRA_TURN)
        val += 60;
    if (result == SOW_GAME_ENDED) {
        dif = tb[COMPUTER_STORE] - tb[HUMAN_STORE];
        val += absolute_value(dif) * 20;
    }
    return val;
}

/* Evaluate a simulated board for the computer. */
static int board_score(const uint8_t b[])
{
    int hs = 0;
    int cs = 0;
    int hm = 0;
    int cm = 0;
    int cdif;
    int pdif;
    int mdif;

    for (int i = 0; i < HUMAN_STORE; i++) {
        hs += b[i];
        if (b[i] > 0)
            hm++;
    }
    for (int i = 7; i < COMPUTER_STORE; i++) {
        cs += b[i];
        if (b[i] > 0)
            cm++;
    }
    if (hs == 0 || cs == 0)
        return ((b[COMPUTER_STORE] + cs) - (b[HUMAN_STORE] + hs)) * 1000;
    cdif = b[COMPUTER_STORE] - b[HUMAN_STORE];
    pdif = cs - hs;
    mdif = cm - hm;
    return (cdif * 100) + (pdif * 4) + (mdif * 3);
}

/* Build a tactical move order. */
static int order_moves(const uint8_t b[], enum player p, int mv[])
{
    int n = 0;
    int lo;
    int hi;
    int va[6];

    if (p == PLAYER_HUMAN) {
        lo = 0;
        hi = HUMAN_STORE;
    } else {
        lo = 7;
        hi = COMPUTER_STORE;
    }
    for (int i = lo; i < hi; i++) {
        if (b[i] > 0) {
            mv[n] = i;
            va[n] = move_order_score(b, i, p);
            n++;
        }
    }
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if ((p == PLAYER_COMPUTER && va[j] > va[i]) ||
                (p == PLAYER_HUMAN && va[j] < va[i])) {
                int tmp = va[i];
                va[i] = va[j];
                va[j] = tmp;
                tmp = mv[i];
                mv[i] = mv[j];
                mv[j] = tmp;
            }
        }
    }
    return n;
}

/* Alpha-beta score for simulated play. */
static int alpha_beta(const uint8_t b[], enum player p, int depth, int alpha,
                      int beta, bool order)
{
    int n;
    int best;
    int mv[6];
    uint8_t nb[TOTAL_PITS];

    if (depth <= 0)
        return board_score(b);
    if (!board_has_move(b, p))
        return board_score(b);
    /* Order moves only at upper nodes.  At depth==1 the children are leaves,
     * so order_moves()'s per-move board_copy+board_sow costs more than the
     * pruning it buys. */
    if (order && depth > 1) {
        n = order_moves(b, p, mv);
    } else {
        n = 0;
        if (p == PLAYER_HUMAN) {
            for (int i = 0; i < HUMAN_STORE; i++)
                if (b[i] > 0)
                    mv[n++] = i;
        } else {
            for (int i = 7; i < COMPUTER_STORE; i++)
                if (b[i] > 0)
                    mv[n++] = i;
        }
    }

    best = (p == PLAYER_COMPUTER) ? -30000 : 30000;

    if (p == PLAYER_HUMAN) {
        for (int i = 0; i < n; i++) {
            enum sow_result result;
            int val;

            board_copy(nb, b);
            result = board_sow(nb, mv[i], p);
            val = alpha_beta(nb, result == SOW_EXTRA_TURN ? p : PLAYER_COMPUTER,
                             depth - 1, alpha, beta, order);
            if (val < best)
                best = val;
            if (best < beta)
                beta = best;
            if (alpha >= beta)
                return best;
        }
    } else {
        for (int i = 0; i < n; i++) {
            enum sow_result result;
            int val;

            board_copy(nb, b);
            result = board_sow(nb, mv[i], p);
            val = alpha_beta(nb, result == SOW_EXTRA_TURN ? p : PLAYER_HUMAN,
                             depth - 1, alpha, beta, order);
            if (val > best)
                best = val;
            if (best > alpha)
                alpha = best;
            if (alpha >= beta)
                return best;
        }
    }
    return best;
}

/* Pick a random legal computer pit. */
static int random_pick(void)
{
    int n = 0;
    int pick;

    for (int i = 7; i < COMPUTER_STORE; i++)
        if (board[i] > 0)
            n++;
    if (n == 0)
        return 7;
    pick = (int)(random_word() % n);
    for (int i = 7; i < COMPUTER_STORE; i++) {
        if (board[i] > 0) {
            if (pick == 0)
                return i;
            pick--;
        }
    }
    return 7;
}

/* --- selection / level / undo ---------------------------------------- */

/* Move the selected human pit by d. */
static void move_selection(int d)
{
    int old = selected;

    selected += d;
    if (selected < 0)
        selected = PITS_PER_SIDE - 1;
    if (selected >= PITS_PER_SIDE)
        selected = 0;
    if (old == selected)
        return;
    draw_pit(old, false);
    draw_pit(selected, true);
}

/* Select a human pit by number. */
static void select_pit(int n)
{
    int old;

    if (n < 0 || n >= PITS_PER_SIDE)
        return;
    old = selected;
    selected = n;
    if (old == selected)
        return;
    draw_pit(old, false);
    draw_pit(selected, true);
}

/* Change the computer level during play. */
static void set_difficulty(enum difficulty_level new_level)
{
    difficulty = new_level;
    draw_status();
    if (difficulty == LEVEL_EASY)
        show_message("COMPUTER LEVEL E: EASY");
    else if (difficulty == LEVEL_MEDIUM)
        show_message("COMPUTER LEVEL M: MEDIUM");
    else
        show_message("COMPUTER LEVEL H: HARD");
}

/* Save state before a player move. */
static void save_undo_state(void)
{
    for (int i = 0; i < TOTAL_PITS; i++)
        undo_board[i] = board[i];
    undo_selected = selected;
    undo_available = true;
}

/* Restore the saved player move. */
static void restore_undo_state(void)
{
    if (!undo_available) {
        show_message("NOTHING TO UNDO");
        return;
    }
    for (int i = 0; i < TOTAL_PITS; i++)
        board[i] = undo_board[i];
    selected = undo_selected;
    game_over = false;
    escape_state = ESCAPE_STATE_NONE;
    undo_available = false;
    draw_positions();
    show_message("LAST MOVE UNDONE");
}

/* --- turn drivers ---------------------------------------------------- */

/* Process the human turn. */
static void play_human_turn(void)
{
    show_message("YOUR TURN: CHOOSE A PIT WITH BEANS");
    while (!quit_requested && !game_over) {
        int c = read_key();
        enum key_action action = decode_key(c);

        if (c >= '1' && c <= '6') {
            select_pit(c - '1');
            continue;
        }
        if (c == 'E' || c == 'e') {
            set_difficulty(LEVEL_EASY);
            continue;
        }
        if (c == 'M' || c == 'm') {
            set_difficulty(LEVEL_MEDIUM);
            continue;
        }
        if (c == 'H' || c == 'h') {
            set_difficulty(LEVEL_HARD);
            continue;
        }
        if (c == '?' || c == 'T' || c == 't') {
            show_hint();
            continue;
        }
        if (c == 'U' || c == 'u') {
            restore_undo_state();
            continue;
        }
        if (action == KEY_ACTION_NONE)
            continue;
        if (action == KEY_ACTION_QUIT) {
            quit_requested = true;
            return;
        }
        if (action == KEY_ACTION_LEFT) {
            move_selection(-1);
        } else if (action == KEY_ACTION_RIGHT) {
            move_selection(1);
        } else if (action == KEY_ACTION_SOW) {
            if (board[selected] == 0) {
                show_message("THAT PIT IS EMPTY");
            } else {
                enum sow_result result;

                save_undo_state();
                result = play_move(selected, PLAYER_HUMAN);
                if (result == SOW_EXTRA_TURN && !game_over)
                    show_message("YOU LANDED IN YOUR STORE: GO AGAIN");
                else
                    return;
            }
        }
    }
}

/* Pick the computer's pit. */
static int computer_pick(void)
{
    int n;
    int best = 7;
    int bval = -30000;
    int alpha = -30000;
    int depth;
    bool order;
    int mv[6];
    uint8_t nb[TOTAL_PITS];

    if (difficulty == LEVEL_EASY && (random_word() % 4) == 0)
        return random_pick();

    if (difficulty == LEVEL_EASY)
        depth = 2;
    else if (difficulty == LEVEL_MEDIUM)
        depth = 3;
    else
        depth = 5;
    order = (difficulty >= LEVEL_HARD);

    if (order) {
        n = order_moves(board, PLAYER_COMPUTER, mv);
    } else {
        n = 0;
        for (int i = 7; i < COMPUTER_STORE; i++)
            if (board[i] > 0)
                mv[n++] = i;
    }
    for (int j = 0; j < n; j++) {
        int i = mv[j];
        enum sow_result result;
        int val;

        board_copy(nb, board);
        result = board_sow(nb, i, PLAYER_COMPUTER);
        val = alpha_beta(nb,
                         result == SOW_EXTRA_TURN ? PLAYER_COMPUTER : PLAYER_HUMAN,
                         depth - 1, alpha, 30000, order);
        val += move_order_score(board, i, PLAYER_COMPUTER) / 10;
        if (val > bval) {
            bval = val;
            best = i;
        }
        if (val > alpha)
            alpha = val;
    }
    return best;
}

/* Pick a hard hint for the human. */
static int hint_pick(void)
{
    int best = -1;
    int bval = 30000;
    uint8_t nb[TOTAL_PITS];

    for (int i = 0; i < HUMAN_STORE; i++) {
        enum sow_result result;
        int val;

        if (board[i] == 0)
            continue;
        board_copy(nb, board);
        result = board_sow(nb, i, PLAYER_HUMAN);
        val = alpha_beta(nb,
                         result == SOW_EXTRA_TURN ? PLAYER_HUMAN : PLAYER_COMPUTER,
                         4, -30000, bval, true);
        val -= move_order_score(board, i, PLAYER_HUMAN) / 10;
        if (val < bval) {
            bval = val;
            best = i;
        }
    }
    return best;
}

/* Show a human hint. */
static void show_hint(void)
{
    int i;

    show_message("THINKING ABOUT A HINT...");
    i = hint_pick();
    if (i < 0) {
        show_message("NO HINT: YOU HAVE NO LEGAL MOVE");
    } else {
        select_pit(i);
        show_message("HINT: TRY THE HIGHLIGHTED PIT");
    }
}

/* Small thinking pause. */
static void think_pause(void)
{
    flush();
    for (int i = 0; i < 250; i++)
        for (int j = 0; j < 120; j++)
            ;
}

/* Run the computer turns. */
static void computer_turn(void)
{
    while (!quit_requested && !game_over) {
        int i;
        enum sow_result result;

        show_message("COMPUTER IS THINKING...");
        think_pause();
        i = computer_pick();
        show_message("COMPUTER SOWS");
        result = play_move(i, PLAYER_COMPUTER);
        if (result == SOW_EXTRA_TURN && !game_over) {
            show_message("COMPUTER LANDED IN ITS STORE: AGAIN");
            think_pause();
        } else {
            return;
        }
    }
}

/* Show the winner. */
static void show_result(void)
{
    clear_note_row(26);
    clear_note_row(27);
    clear_note_row(28);
    set_color(37);
    cursor_move(26, 5);
    if (quit_requested)
        write_text("GAME QUIT");
    else if (board[HUMAN_STORE] > board[COMPUTER_STORE])
        write_text("YOU WIN! MORE BEANS IN YOUR STORE");
    else if (board[HUMAN_STORE] < board[COMPUTER_STORE])
        write_text("COMPUTER WINS THIS HARVEST");
    else
        write_text("DRAW GAME");
    cursor_move(27, 5);
    write_text("FINAL  YOU:");
    write_padded_number(board[HUMAN_STORE]);
    write_text("  CPU:");
    write_padded_number(board[COMPUTER_STORE]);
    reset_color();
    flush();
}

/* Ask whether to start another game. */
static bool ask_new_game(void)
{
    if (quit_requested)
        return false;
    clear_note_row(28);
    set_color(37);
    cursor_move(28, 5);
    write_text("NEW GAME?  Y/N");
    reset_color();
    while (true) {
        int c = read_key();

        if (c == 'Y' || c == 'y')
            return true;
        if (c == 'N' || c == 'n' || c == 'Q' || c == 'q' ||
            c == ESCAPE_KEY || c == CTRL_C_KEY)
            return false;
    }
}

/* Game entry point. */
int main(void)
{
    /* Install the single 8 KB console buffer once at startup. */
    setvbuf(stdout, console_buffer, _IOFBF, CONSOLE_BUFFER_SIZE);

    initialize_game();
    while (!quit_requested) {
        draw_all();
        while (!quit_requested && !game_over) {
            if (has_legal_move(PLAYER_HUMAN))
                play_human_turn();
            if (!quit_requested && !game_over && has_legal_move(PLAYER_COMPUTER))
                computer_turn();
            if (!has_legal_move(PLAYER_HUMAN) || !has_legal_move(PLAYER_COMPUTER)) {
                collect_endgame();
                draw_positions();
            }
        }
        show_result();
        if (!ask_new_game())
            break;
        {
            enum difficulty_level saved_difficulty = difficulty;

            initialize_game();
            difficulty = saved_difficulty;
        }
    }

    cursor_move(SCREEN_HEIGHT, 1);
    show_cursor();
    reset_color();
    write_text("\r\n");
    flush();
    return 0;
}
