#include <assert.h>
#include <stdio.h>
#include <string.h>

int bios(int fn, int byte);
int bdos(int fn, int value);

#define main chess_main
#include "../CHESS.C"
#undef main

static unsigned char output[65536], input[256];
static unsigned output_count, input_count, input_position;
static int timer_active;

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
    timer_active = 1;
    return 0;
}

int svc_timer_active(int id)
{
    assert(id == 0);
    return timer_active;
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
    queue("q");
    assert(chess_main() == 0);
    assert(strstr((char *)output, "\033[0m\033[?25hThanks for playing CHESS!\r\n"));
    puts("CHESS human/CPU, tip, promotion, replay and quit PASS");
}

int main(void)
{
    raw_console_tests();
    keyboard_tests();
    gameplay_tests();
    return 0;
}
