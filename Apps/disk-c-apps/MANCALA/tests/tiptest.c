#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_putchar(int c);
static int test_fflush(FILE *stream);

#define putchar test_putchar
#define fflush test_fflush
/* dcc treats bdos specially unless the test substitutes a different name. */
#define bdos test_bdos
#define main mancala_main
#include "../manc11.c"
#undef main
#undef bdos
#undef fflush
#undef putchar

static char output[4096];
static int output_used;
static const char *keys;
static int random_calls;
static unsigned seed = 12345;

static void check(bool condition, const char *message)
{
    if (!condition) {
        printf("FAIL: %s\n", message);
        exit(1);
    }
}

static int test_putchar(int c)
{
    check(output_used < (int)sizeof(output) - 1, "output buffer capacity");
    output[output_used++] = (char)c;
    output[output_used] = 0;
    return c;
}

static int test_fflush(FILE *stream)
{
    (void)stream;
    return 0;
}

int test_bdos(int function, int argument)
{
    check(function == 6 && argument == 255, "nonblocking input");
    check(keys != NULL && *keys != 0, "input sequence exhausted");
    return *keys++;
}

unsigned svc_random_u16(void)
{
    random_calls++;
    return 1;
}

static void reset_test(void)
{
    initialize_game();
    output_used = 0;
    output[0] = 0;
    random_calls = 0;
    keys = NULL;
}

static void forced_loss_tests(void)
{
    static const uint8_t loss[TOTAL_PITS] =
        {0, 0, 0, 0, 0, 1, 7, 1, 0, 0, 0, 0, 0, 39};
    static const uint8_t cpu_loss[TOTAL_PITS] =
        {1, 0, 0, 0, 0, 0, 39, 0, 0, 0, 0, 0, 1, 7};
    uint8_t saved[TOTAL_PITS];

    reset_test();
    board_copy(board, loss);
    board_copy(saved, board);
    check(tip_pick() == 5, "tip must recommend the only legal losing move");
    check(memcmp(board, saved, sizeof(saved)) == 0, "tip must not change board");

    board_copy(board, cpu_loss);
    for (int level = LEVEL_EASY; level <= LEVEL_HARD; level++) {
        difficulty = (enum difficulty_level)level;
        check(computer_pick() == 12, "CPU must choose its only legal losing move");
    }
}

static void score_range_tests(void)
{
    int previous = 30000;

    reset_test();
    memset(board, 0, sizeof(board));
    for (int human = 0; human <= 48; human++) {
        int score;

        board[HUMAN_STORE] = (uint8_t)human;
        board[COMPUTER_STORE] = (uint8_t)(48 - human);
        score = board_score(board);
        check(score > -30000 && score < 30000, "terminal score inside search bounds");
        check(score < previous, "terminal score preserves ordering without overflow");
        check((human < 24 && score > 0) || (human == 24 && score == 0) ||
              (human > 24 && score < 0), "terminal score has correct winner");
        previous = score;
    }
}

/* Exhaustive minimax without pruning is the oracle for alpha-beta selection. */
static int oracle(const uint8_t b[], enum player p, int depth)
{
    int best = p == PLAYER_HUMAN ? 30000 : -30000;
    int lo = p == PLAYER_HUMAN ? 0 : 7;
    int hi = p == PLAYER_HUMAN ? HUMAN_STORE : COMPUTER_STORE;
    uint8_t next[TOTAL_PITS];

    if (depth == 0 || !board_has_move(b, p))
        return board_score(b);
    for (int i = lo; i < hi; i++) {
        enum sow_result result;
        enum player other = p == PLAYER_HUMAN ? PLAYER_COMPUTER : PLAYER_HUMAN;
        int value;

        if (b[i] == 0)
            continue;
        board_copy(next, b);
        result = board_sow(next, i, p);
        value = oracle(next, result == SOW_EXTRA_TURN ? p : other, depth - 1);
        if ((p == PLAYER_HUMAN && value < best) ||
            (p == PLAYER_COMPUTER && value > best))
            best = value;
    }
    return best;
}

static int oracle_pick(enum player p, int depth, bool ordered)
{
    int moves[6];
    int count = 0;
    int best = -1;
    int best_value = p == PLAYER_HUMAN ? 30000 : -30000;
    int lo = p == PLAYER_HUMAN ? 0 : 7;
    int hi = p == PLAYER_HUMAN ? HUMAN_STORE : COMPUTER_STORE;
    uint8_t next[TOTAL_PITS];

    if (ordered) {
        count = order_moves(board, p, moves);
    } else {
        for (int i = lo; i < hi; i++)
            if (board[i] > 0)
                moves[count++] = i;
    }
    for (int j = 0; j < count; j++) {
        int pit = moves[j];
        enum sow_result result;
        enum player other = p == PLAYER_HUMAN ? PLAYER_COMPUTER : PLAYER_HUMAN;
        int value;
        int bonus = move_order_score(board, pit, p) / (10 * SCORE_SCALE);

        board_copy(next, board);
        result = board_sow(next, pit, p);
        value = oracle(next, result == SOW_EXTRA_TURN ? p : other, depth - 1);
        value += p == PLAYER_HUMAN ? -bonus : bonus;
        if (best < 0 || (p == PLAYER_HUMAN && value < best_value) ||
            (p == PLAYER_COMPUTER && value > best_value)) {
            best_value = value;
            best = pit;
        }
    }
    return best;
}

static unsigned next_random(void)
{
    seed = (unsigned)((seed * 25173U + 13849U) & 0xffffU);
    return seed;
}

static void search_tests(void)
{
    uint8_t saved[TOTAL_PITS];

    for (int sample = 0; sample < 256; sample++) {
        int beans = 2 + (int)(next_random() % 11);
        int human = (int)(next_random() % (unsigned)(49 - beans));

        reset_test();
        memset(board, 0, sizeof(board));
        board[HUMAN_STORE] = (uint8_t)human;
        board[COMPUTER_STORE] = (uint8_t)(48 - beans - human);
        board[next_random() % 6]++;
        board[7 + next_random() % 6]++;
        for (int i = 2; i < beans; i++) {
            int pit = (int)(next_random() % 12);
            board[pit < 6 ? pit : pit + 1]++;
        }
        board_copy(saved, board);
        check(tip_pick() == oracle_pick(PLAYER_HUMAN, 5, false),
              "tip matches exhaustive five-ply minimax with tactical bonus");
        for (int level = LEVEL_MEDIUM; level <= LEVEL_HARD; level++) {
            difficulty = (enum difficulty_level)level;
            check(computer_pick() ==
                  oracle_pick(PLAYER_COMPUTER, level == LEVEL_HARD ? 5 : 3,
                              level == LEVEL_HARD),
                  "CPU matches exhaustive minimax with tactical bonus");
        }
        check(memcmp(board, saved, sizeof(saved)) == 0, "search leaves board unchanged");
        check(random_calls == 0, "tips and non-Easy CPU do not use randomness");
    }
}

static void tip_controls_tests(void)
{
    static const uint8_t loss[TOTAL_PITS] =
        {0, 0, 0, 0, 0, 1, 7, 1, 0, 0, 0, 0, 0, 39};
    uint8_t saved[TOTAL_PITS];

    reset_test();
    draw_help();
    check(strstr(output, "T TIP") != NULL, "help advertises T TIP");
    check(strstr(output, "HINT") == NULL && strstr(output, "?") == NULL,
          "help has no old hint shortcut");

    for (int uppercase = 0; uppercase < 2; uppercase++) {
        reset_test();
        board_copy(board, loss);
        board_copy(saved, board);
        undo_available = true;
        keys = uppercase ? "Tq" : "tq";
        play_human_turn();
        check(selected == 5, "T/t highlights a legal pit");
        check(strstr(output, "TIP: TRY THE HIGHLIGHTED PIT") != NULL,
              "tip success message");
        check(strstr(output, "NO TIP") == NULL, "losing move is still a tip");
        check(memcmp(board, saved, sizeof(saved)) == 0 && undo_available,
              "tip preserves board and undo state");
    }

    reset_test();
    board_copy(board, loss);
    keys = "?q";
    play_human_turn();
    check(selected == 0 && strstr(output, "TIP") == NULL &&
          strstr(output, "HINT") == NULL, "question mark no longer requests a tip");

    reset_test();
    memset(board, 0, sizeof(board));
    board[HUMAN_STORE] = 24;
    board[COMPUTER_STORE] = 24;
    check(tip_pick() == -1, "no recommendation when all human pits are empty");
    show_tip();
    check(strstr(output, "NO TIP: YOU HAVE NO LEGAL MOVE") != NULL,
          "genuine no-move tip reports the reason");
}

int main(void)
{
    forced_loss_tests();
    score_range_tests();
    puts("MANCALA forced losses and 16-bit score range PASS");
    search_tests();
    puts("MANCALA exhaustive minimax oracle PASS");
    tip_controls_tests();
    puts("MANCALA tip, score range, minimax and controls PASS");
    return 0;
}
