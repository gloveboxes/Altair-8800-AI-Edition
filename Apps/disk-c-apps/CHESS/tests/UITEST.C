#include <assert.h>
#include <stdio.h>
#include <string.h>

int bios(int fn, int byte);
int bdos(int fn, int value);
static int fake_file_bdos(int fn, void *arg);

#define CHESS_FILE_BDOS fake_file_bdos
#define main chess_main
#include "../CHESS.C"
#undef main

static unsigned char output[65536], input[256];
static unsigned output_count, input_count, input_position;
static int timer_active;

/* One-file in-memory CP/M disk for CHESS.SAV, with write-fault injection. */
static unsigned char disk_data[64][128], *disk_dma;
static unsigned disk_records;
static int disk_exists, disk_fail_writes;

static int fake_file_bdos(int fn, void *arg)
{
    unsigned char *fcb = arg;

    if (fn == F_DMA) {
        disk_dma = arg;
        return 0;
    }
    assert(memcmp(fcb + 1, "CHESS   SAV", 11) == 0 && fcb[0] == 0);
    if (fn == F_OPEN || fn == F_DELETE) {
        if (!disk_exists) return 255;
        if (fn == F_DELETE) disk_exists = 0;
        fcb[32] = 0;
        return 0;
    }
    if (fn == F_MAKE) {
        assert(!disk_exists);
        disk_exists = 1;
        disk_records = fcb[32] = 0;
        return 0;
    }
    if (fn == F_CLOSE) return disk_exists ? 0 : 255;
    assert(disk_exists && disk_dma == save_record);
    if (fn == F_READ) {
        if (fcb[32] >= disk_records) return 1;
        memcpy(disk_dma, disk_data[fcb[32]++], 128);
        return 0;
    }
    assert(fn == F_WRITE);
    if (disk_fail_writes || fcb[32] >= 64) return 2;
    memcpy(disk_data[fcb[32]++], disk_dma, 128);
    if (fcb[32] > disk_records) disk_records = fcb[32];
    return 0;
}

int bios(int fn, int byte)
{
    assert(fn == 4 && output_count < sizeof(output) - 1);
    output[output_count++] = (unsigned char)byte;
    output[output_count] = 0;
    return 0;
}

int bdos(int fn, int value)
{
    assert(fn == 6 && value == 255);
    return input_position < input_count ? input[input_position++] : 0;
}

int svc_start_timer(int id, unsigned duration)
{
    assert(id == 0 && duration == 120);
    timer_active = 3;
    return 0;
}

/* Expires after a few polls so loops waiting on a lone ESC can finish. */
int svc_timer_active(int id)
{
    assert(id == 0);
    if (timer_active) --timer_active;
    return timer_active != 0;
}

static void reset_output(void)
{
    console_flush();
    output_count = 0;
    output[0] = 0;
}

static void queue(const char *text)
{
    input_count = (unsigned)strlen(text);
    assert(input_count < sizeof(input));
    memcpy(input, text, input_count);
    input_position = 0;
}

static void raw_console_tests(void)
{
    unsigned i;
    const char *expected = "\033[5;18H\033[107m$\r\n0 32767 -32768";

    cursor(5, 18);
    color(107);
    write_text("$\r\n");
    write_number(0);
    write_byte(' ');
    write_number(32767);
    write_byte(' ');
    write_number(-32768);
    assert(output_count == 0);
    console_flush();
    assert(strcmp((char *)output, expected) == 0);
    reset_output();
    for (i = 0; i < CONSOLE_BUFFER_SIZE - 1; ++i) write_byte('.');
    write_text("\033[31m$");
    write_number(-32768);
    console_flush();
    assert(output_count == CONSOLE_BUFFER_SIZE - 1 + 12);
    assert(memcmp(output + CONSOLE_BUFFER_SIZE - 1, "\033[31m$-32768", 12) == 0);
    reset_output();
    for (i = 0; i < 256; ++i) write_byte((unsigned char)i);
    console_flush();
    console_flush();
    assert(output_count == 256);
    for (i = 0; i < 256; ++i) assert(output[i] == i);
    reset_output();
    queue("\023\004");
    start_game();
    assert(output_count > CONSOLE_BUFFER_SIZE);
    assert(input_position == 0);
    assert(read_action() == KEY_LEFT && read_action() == KEY_RIGHT);
    assert(strstr((char *)output, "CHESS") && strstr((char *)output, "Your turn"));
    reset_output();
    puts("CHESS raw console and drawing-time input PASS");
}

static void keyboard_tests(void)
{
    queue("\033[A\033OB\033[C\033[D\005\030\023\004");
    assert(read_action() == 0 && read_action() == 0 && read_action() == KEY_UP);
    assert(read_action() == 0 && read_action() == 0 && read_action() == KEY_DOWN);
    assert(read_action() == 0 && read_action() == 0 && read_action() == KEY_RIGHT);
    assert(read_action() == 0 && read_action() == 0 && read_action() == KEY_LEFT);
    assert(read_action() == KEY_UP && read_action() == KEY_DOWN);
    assert(read_action() == KEY_LEFT && read_action() == KEY_RIGHT);
    queue("\033");
    assert(read_action() == 0 && read_action() == 0);
    timer_active = 0;
    assert(read_action() == KEY_ESCAPE);
    queue("q\003t\n");
    assert(read_action() == KEY_QUIT && read_action() == KEY_QUIT);
    assert(read_action() == 'T' && read_action() == 10);
    puts("CHESS arrows, Escape timeout and controls PASS");
}

static void gameplay_tests(void)
{
    static signed char before[128];
    static unsigned char before_history[HISTORY_SIZE][POSITION_BYTES];
    Move m;
    int p, tip_source;

    start_game();
    handle_action(KEY_LEFT);
    assert(cursor_square == 19);
    handle_action(KEY_RIGHT);
    assert(cursor_square == 20);
    handle_action(' ');
    assert(selected_square == 20 && legal_targets[36] && legal_targets[52]);
    handle_action(KEY_ESCAPE);
    assert(selected_square == -1 && !quit_requested);
    handle_action(' ');
    handle_action(KEY_UP);
    handle_action(KEY_UP);
    handle_action(13);
    assert(board[52] == PAWN && board[20] == 0 && side == BLACK);
    assert(choose_move(&m, level_depth(level)));
    play_move(&m);
    assert(side == WHITE && !game_over && !engine_failed);
    reset_output();
    memcpy(before, board, sizeof(board));
    memcpy(before_history, history, sizeof(history));
    show_tip();
    assert(hint_from >= 0 && hint_to >= 0 && tip_move[0]);
    assert(memcmp(before, board, sizeof(board)) == 0);
    assert(memcmp(before_history, history, sizeof(history)) == 0);
    assert(side == WHITE && selected_square == -1);
    tip_source = hint_from;
    handle_action(' ');
    assert(selected_square == tip_source);
    reset_output();
    for (p = KNIGHT; p <= QUEEN; ++p) {
        new_position();
        memset(board, 0, sizeof(board));
        board[7] = KING;
        board[119] = -KING;
        board[96] = PAWN;
        kings[0] = 7;
        kings[1] = 119;
        castle_rights = 0;
        history_count = 0;
        record_position();
        game_over = 0;
        cursor_square = 96;
        clear_selection();
        finish_turn();
        handle_action(' ');
        handle_action(KEY_UP);
        handle_action(' ');
        assert(promotion_pending && side == WHITE);
        handle_action((int)('6' - p));
        assert(!promotion_pending && board[112] == p && side == BLACK);
        reset_output();
    }
    handle_action('N');
    assert(side == WHITE && board[20] == PAWN && !game_over);
    handle_action('3');
    assert(level == 3);
    memcpy(before, board, sizeof(board));
    queue("q");
    show_tip();
    assert(quit_requested && memcmp(before, board, sizeof(board)) == 0);
    reset_output();
    quit_requested = 0;
    queue("qn");
    assert(chess_main() == 0);
    assert(strstr((char *)output, "\033[0m\033[?25hThanks for playing CHESS!\r\n"));
    puts("CHESS human/CPU, tip, promotion, replay and quit PASS");
}

static void save_tests(void)
{
    static signed char saved_board[128];
    static unsigned char saved_history[HISTORY_SIZE][POSITION_BYTES];
    char saved_last[8];
    int saved_count, saved_moves;
    Move m;

    assert(!disk_exists && !load_game());
    start_game();
    handle_action(' ');
    handle_action(KEY_UP);
    handle_action(KEY_UP);
    handle_action(13);
    assert(side == BLACK && choose_move(&m, level_depth(level)));
    play_move(&m);
    handle_action('2');
    wins = 3;
    losses = 2;
    draws = 1;
    memcpy(saved_board, board, sizeof(board));
    memcpy(saved_history, history, sizeof(history));
    memcpy(saved_last, last_move, sizeof(last_move));
    saved_count = history_count;
    saved_moves = full_moves;

    quit_requested = 1;
    queue("\033");
    assert(confirm_quit() == 0 && !quit_requested && !disk_exists);
    quit_requested = 1;
    queue("y");
    assert(confirm_quit() == 2 && disk_exists);
    assert(disk_records == (unsigned)(SAVE_HEADER + saved_count * POSITION_BYTES + 127) / 128);

    handle_action('1');
    start_game();
    wins = losses = draws = 0;
    assert(load_game());
    assert(memcmp(board, saved_board, sizeof(board)) == 0);
    assert(history_count == saved_count && full_moves == saved_moves);
    assert(memcmp(history, saved_history, (size_t)saved_count * POSITION_BYTES) == 0);
    assert(strcmp(last_move, saved_last) == 0);
    assert(side == WHITE && level == 2 && !game_over);
    assert(wins == 3 && losses == 2 && draws == 1);
    assert(strstr(message, "Saved game restored"));
    assert(generate_moves(0) > 0);

    disk_data[0][SAVE_BOARD + 4] = 0;
    assert(!load_game());
    disk_data[0][SAVE_BOARD + 4] = KING;
    disk_data[0][0] = 'X';
    assert(!load_game());
    disk_data[0][0] = 'C';
    --disk_records;
    assert(!load_game());
    ++disk_records;
    assert(load_game());

    disk_fail_writes = 1;
    quit_requested = 1;
    queue("y\033");
    assert(confirm_quit() == 0 && !disk_exists);
    assert(strstr((char *)output, "ERROR: could not write CHESS.SAV"));
    disk_fail_writes = 0;
    quit_requested = 1;
    queue("y");
    assert(confirm_quit() == 2 && disk_exists);
    quit_requested = 1;
    queue("n");
    assert(confirm_quit() == 1 && !disk_exists);
    queue("qy");
    quit_requested = 0;
    reset_output();
    assert(chess_main() == 0 && disk_exists);
    assert(strstr((char *)output, "Game saved to CHESS.SAV"));
    disk_exists = 0;
    reset_output();
    puts("CHESS save prompt, save/restore round trip and corrupt-file rejection PASS");
}

int main(void)
{
    raw_console_tests();
    keyboard_tests();
    gameplay_tests();
    save_tests();
    return 0;
}
