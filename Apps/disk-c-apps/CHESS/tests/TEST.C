#define CHESS_TEST
#include "../CHESS.C"
#include <stdio.h>

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAIL: %s\n", name);
        ++failures;
    }
}

static int square(const char *text)
{
    return (text[1] - '1') * 16 + text[0] - 'a';
}

static void load_fen(const char *fen)
{
    const char *pieces = "PNBRQK";
    const char *found;
    int rank = 7, file = 0, p;
    char ch;

    memset(board, 0, sizeof(board));
    castle_rights = 0;
    ep_square = -1;
    halfclock = 0;
    history_count = 0;
    engine_failed = 0;
    while (*fen && *fen != ' ') {
        ch = *fen++;
        if (ch == '/') {
            --rank;
            file = 0;
        } else if (ch >= '1' && ch <= '8') {
            file += ch - '0';
        } else {
            found = strchr(pieces, ch >= 'a' ? ch - 32 : ch);
            if (!found) {
                check(0, "valid fixture FEN");
                return;
            }
            p = (int)(found - pieces) + 1;
            board[rank * 16 + file] = ch >= 'a' ? -p : p;
            if (p == KING) kings[ch >= 'a' ? 1 : 0] = rank * 16 + file;
            ++file;
        }
    }
    ++fen;
    side = *fen == 'w' ? WHITE : BLACK;
    fen += 2;
    while (*fen && *fen != ' ') {
        if (*fen == 'K') castle_rights |= 1;
        if (*fen == 'Q') castle_rights |= 2;
        if (*fen == 'k') castle_rights |= 4;
        if (*fen == 'q') castle_rights |= 8;
        ++fen;
    }
    ++fen;
    if (*fen != '-') ep_square = square(fen);
    while (*fen && *fen != ' ') ++fen;
    if (*fen) ++fen;
    while (*fen >= '0' && *fen <= '9') halfclock = halfclock * 10 + *fen++ - '0';
    record_position();
}

static int find_move(const char *text, int promotion, Move *m)
{
    int i;

    generate_moves(0);
    for (i = 0; i < move_count[0]; ++i)
        if (moves[0][i].from == square(text)
            && moves[0][i].to == square(text + 2)
            && moves[0][i].promotion == promotion) {
            *m = moves[0][i];
            return 1;
        }
    return 0;
}

static int play(const char *text, int promotion)
{
    Move m;
    Undo u;

    if (!find_move(text, promotion, &m)) {
        check(0, text);
        return 0;
    }
    make_move(&m, &u);
    record_position();
    return 1;
}

static unsigned long perft(int depth, int ply)
{
    unsigned long count = 0;
    int i;
    Undo u;

    if (!depth) return 1;
    generate_moves(ply);
    if (depth == 1) return (unsigned long)move_count[ply];
    for (i = 0; i < move_count[ply]; ++i) {
        make_move(&moves[ply][i], &u);
        count += perft(depth - 1, ply + 1);
        unmake_move(&moves[ply][i], &u);
    }
    return count;
}

static void perft_case(const char *fen, int depth, unsigned long expected)
{
    unsigned char before[POSITION_BYTES], after[POSITION_BYTES];
    unsigned long count;
    int old_clock;

    load_fen(fen);
    old_clock = halfclock;
    position_key(before);
    count = perft(depth, 0);
    position_key(after);
    if (count != expected) {
        printf("Perft expected %lu, got %lu\n", expected, count);
        check(0, fen);
    }
    check(memcmp(before, after, POSITION_BYTES) == 0 && halfclock == old_clock,
          "perft restores position");
    check(!engine_failed, "move capacity");
}

static void perft_tests(void)
{
    const char *start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const char *kiwi = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    const char *endgame = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    const char *promotion = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
    const char *tactics = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    const char *middle = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";

    perft_case(start, 1, 20UL);
    perft_case(start, 2, 400UL);
    perft_case(start, 3, 8902UL);
    perft_case(kiwi, 2, 2039UL);
    perft_case(endgame, 2, 191UL);
    perft_case(promotion, 2, 264UL);
    perft_case(tactics, 2, 1486UL);
    perft_case(middle, 2, 2079UL);
#ifndef _DCC_
    perft_case(start, 4, 197281UL);
    perft_case(kiwi, 3, 97862UL);
    perft_case(endgame, 3, 2812UL);
    perft_case(promotion, 3, 9467UL);
    perft_case(tactics, 3, 62379UL);
    perft_case(middle, 3, 89890UL);
#endif
    puts("CHESS perft PASS");
}

static void rule_tests(void)
{
    Move m;
    Undo u;
    unsigned char key[POSITION_BYTES];
    int i;

    new_position();
    check(!find_move("e2e5", 0, &m), "reject triple pawn push");
    check(!find_move("e1e2", 0, &m), "reject own-piece capture");
    play("e2e4", 0);
    check(ep_square == square("e3") && halfclock == 0, "double push state");
    check(effective_ep() == -1, "irrelevant EP omitted");
    play("a7a6", 0);
    play("e4e5", 0);
    play("d7d5", 0);
    check(find_move("e5d6", 0, &m) && m.flags == EP_CAPTURE, "legal en passant");
    make_move(&m, &u);
    check(board[square("d5")] == 0 && board[square("d6")] == PAWN, "EP capture");
    unmake_move(&m, &u);
    check(board[square("d5")] == -PAWN && board[square("e5")] == PAWN,
          "EP restoration");
    play("g1f3", 0);
    check(ep_square == -1, "EP expires");

    load_fen("k7/8/8/r4pPK/8/8/8/8 w - f6 0 1");
    check(!find_move("g5f6", 0, &m) && effective_ep() == -1, "EP cannot expose king");
    load_fen("k7/8/8/8/8/8/4r3/4K3 w - - 0 1");
    check(!find_move("e1e3", 0, &m), "king only one square");
    check(find_move("e1e2", 0, &m), "king may capture undefended checking rook");
    load_fen("k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1");
    check(!find_move("e2d2", 0, &m), "pinned rook cannot expose king");

    load_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    check(find_move("e1g1", 0, &m), "White kingside castle");
    make_move(&m, &u);
    check(board[6] == KING && board[5] == ROOK && !board[4] && !board[7]
          && castle_rights == 12 && kings[0] == 6, "castling state");
    unmake_move(&m, &u);
    check(board[4] == KING && board[7] == ROOK && castle_rights == 15,
          "castling restoration");
    play("h1h2", 0);
    play("h8h7", 0);
    play("h2h1", 0);
    check((castle_rights & 1) == 0 && (castle_rights & 4) == 0,
          "rook return cannot restore castling rights");
    load_fen("4k3/8/8/8/8/8/8/R3K2r w Q - 0 1");
    check(!find_move("e1c1", 0, &m), "cannot castle out of check");
    load_fen("4kr2/8/8/8/8/8/8/4K2R w K - 0 1");
    check(!find_move("e1g1", 0, &m), "cannot castle through check");
    load_fen("4k1r1/8/8/8/8/8/8/4K2R w K - 0 1");
    check(!find_move("e1g1", 0, &m), "cannot castle into check");
    load_fen("4k3/8/8/8/8/8/8/4K3 w KQ - 0 1");
    check(!find_move("e1g1", 0, &m), "castling requires rook");
    load_fen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    check(find_move("e8c8", 0, &m), "Black queenside castle");
    play("a8a1", 0);
    check((castle_rights & 10) == 0, "rook capture clears both rights");

    load_fen("7k/P7/8/8/8/8/8/7K w - - 0 1");
    for (i = KNIGHT; i <= QUEEN; ++i) {
        check(find_move("a7a8", i, &m), "all four promotion choices");
        make_move(&m, &u);
        check(board[112] == i && halfclock == 0, "promotion state");
        unmake_move(&m, &u);
        check(board[96] == PAWN && board[112] == 0, "promotion restoration");
    }
    load_fen("7k/8/8/8/8/8/p7/7K b - - 0 1");
    check(find_move("a2a1", KNIGHT, &m), "Black underpromotion");

    new_position();
    for (i = 0; i < 2; ++i) {
        play("g1f3", 0);
        play("g8f6", 0);
        play("f3g1", 0);
        play("f6g8", 0);
    }
    position_key(key);
    check(repetitions(key) == 3, "exact threefold repetition");
    play("e2e4", 0);
    check(history_count == 1, "irreversible move clears repetition window");
    load_fen("7k/8/8/8/8/8/R7/K7 w - - 99 1");
    play("a2b2", 0);
    check(halfclock == 100, "fifty-move threshold is 100 halfmoves");
    load_fen("7k/8/8/8/8/8/8/K7 w - - 0 1");
    check(insufficient_material(), "bare kings");
    load_fen("7k/8/8/8/8/8/8/KN6 w - - 0 1");
    check(insufficient_material(), "king and knight");
    load_fen("7k/8/8/8/8/8/8/KNN5 w - - 0 1");
    check(!insufficient_material(), "two knights not an automatic dead position");
    load_fen("7k/8/8/8/8/4b3/8/K1B5 w - - 0 1");
    check(insufficient_material(), "bishops on same square color");
    load_fen("7k/8/8/8/8/3b4/8/K1B5 w - - 0 1");
    check(!insufficient_material(), "opposite-color bishops can mate");
    puts("CHESS special rules and draws PASS");
}

static int evaluate(void)
{
    int score = evaluate_white();

    return side > 0 ? score : -score;
}

/* Reference scoring: the original per-square formula, kept independent. */
static int reference_white_score(void)
{
    int s, p, player, rank, file, center, value, score = 0;

    for (s = 0; s < 128; ++s) {
        if (s & 0x88) {
            s += 7;
            continue;
        }
        p = board[s];
        if (!p) continue;
        player = p > 0 ? WHITE : BLACK;
        p = abs(p);
        rank = player == WHITE ? s / 16 : 7 - s / 16;
        file = s % 16;
        center = 14 - abs(file * 2 - 7) - abs(s / 16 * 2 - 7);
        value = piece_value[p];
        if (p == PAWN) value += rank * 8 + center * 2;
        if (p == KNIGHT) value += center * 5;
        if (p == BISHOP) value += center * 3;
        if (p == ROOK && rank == 6) value += 20;
        if (p == KING && rank == 0 && (file == 2 || file == 6)) value += 35;
        score += player * value;
    }
    return score;
}

static int eval_mismatches;

static void eval_walk(int depth, int ply)
{
    int i;
    Undo u;

    if (eval_white != reference_white_score()) ++eval_mismatches;
    if (!depth) return;
    generate_moves(ply);
    for (i = 0; i < move_count[ply]; ++i) {
        make_move(&moves[ply][i], &u);
        eval_walk(depth - 1, ply + 1);
        unmake_move(&moves[ply][i], &u);
    }
}

static void eval_tests(void)
{
    static const char *fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r3k2r/8/8/3pP3/8/8/8/R3K2R w KQkq d6 0 1",
        "r3k2r/1P5P/8/8/8/8/1p5p/R3K2R b KQkq - 0 1",
    };
    unsigned i;

    for (i = 0; i < sizeof(fens) / sizeof(fens[0]); ++i) {
        load_fen(fens[i]);
        eval_white = evaluate_white();
        check(eval_white == reference_white_score(), "full evaluation matches reference");
        check(evaluate() == (side == WHITE ? 1 : -1) * eval_white, "side-relative evaluation");
        eval_mismatches = 0;
        eval_walk(3, 0);
        check(eval_mismatches == 0, "incremental evaluation tracks make/unmake");
    }
    puts("CHESS incremental evaluation PASS");
}

static void search_restoration(int depth)
{
    static signed char before[128];
    static unsigned char old_kings[2];
    static unsigned char old_history[HISTORY_SIZE][POSITION_BYTES];
    int old_side, old_ep, old_clock, old_count;
    unsigned char old_rights;
    Move m, legal;

    memcpy(before, board, sizeof(board));
    memcpy(old_kings, kings, sizeof(kings));
    memcpy(old_history, history, sizeof(history));
    old_side = side;
    old_ep = ep_square;
    old_clock = halfclock;
    old_count = history_count;
    old_rights = castle_rights;
    check(choose_move(&m, depth), "search returns a move");
    check(completed_depth > 0 && completed_depth <= depth && node_count <= NODE_LIMIT,
          "bounded, completed minimax iteration");
    check(memcmp(before, board, sizeof(board)) == 0
          && memcmp(old_kings, kings, sizeof(kings)) == 0
          && memcmp(old_history, history, sizeof(history)) == 0
          && side == old_side && ep_square == old_ep && halfclock == old_clock
          && history_count == old_count && castle_rights == old_rights,
          "CPU/tip search restores all state");
    check(find_move("a1a1", 0, &legal) == 0, "no null move");
    check(eval_white == reference_white_score(), "search restores incremental evaluation");
    generate_moves(0);
    for (old_count = 0; old_count < move_count[0]; ++old_count)
        if (memcmp(&m, &moves[0][old_count], sizeof(Move)) == 0) break;
    check(old_count < move_count[0], "CPU/tip chooses a legal move");
}

/* Independent unpruned oracle for the exact-score comparison. Leaves are
   resolved by an unpruned capture search with the same stand-pat, check and
   queen-promotion rules as the engine's quiescence search. */
static int quiet_oracle(int ply, int qleft)
{
    int i, value, best, stand, in_check, noisy;
    Undo u;

    if (qleft < QUIESCE_PLIES && halfclock == 0 && insufficient_material()) return 0;
    stand = evaluate();
    if (qleft == 0) return stand;
    in_check = attacked(kings[king_index(side)], -side);
    generate_moves(ply);
    if (!move_count[ply] && in_check) return -MATE_SCORE + ply;
    best = in_check ? -INFINITY_SCORE : stand;
    for (i = 0; i < move_count[ply]; ++i) {
        noisy = moves[ply][i].promotion ? moves[ply][i].promotion == QUEEN
            : board[moves[ply][i].to] != 0 || (moves[ply][i].flags & EP_CAPTURE);
        if (!in_check && !noisy) continue;
        make_move(&moves[ply][i], &u);
        value = -quiet_oracle(ply + 1, qleft - 1);
        unmake_move(&moves[ply][i], &u);
        if (value > best) best = value;
    }
    return best;
}

static int oracle(int depth, int ply)
{
    int i, value, best = -INFINITY_SCORE;
    Undo u;

    if (!generate_moves(ply))
        return attacked(kings[king_index(side)], -side) ? -MATE_SCORE + ply : 0;
    if (halfclock >= 100 || insufficient_material()) return 0;
    if (!depth) return quiet_oracle(ply, QUIESCE_PLIES);
    for (i = 0; i < move_count[ply]; ++i) {
        make_move(&moves[ply][i], &u);
        value = -oracle(depth - 1, ply + 1);
        unmake_move(&moves[ply][i], &u);
        if (value > best) best = value;
    }
    return best;
}

static void oracle_case(const char *fen, int depth, const char *name)
{
    Move m;
    Undo u;
    int expected;

    load_fen(fen);
    eval_white = evaluate_white();
    expected = oracle(depth, 0);
    check(choose_move(&m, depth) && completed_depth == depth
          && search_score == expected, name);
    make_move(&m, &u);
    check(-oracle(depth - 1, 1) == expected, "chosen move realizes the oracle score");
    unmake_move(&m, &u);
}

static void search_tests(void)
{
    Move m;
    Undo u;
    unsigned char before[POSITION_BYTES], after[POSITION_BYTES];
    int depth;

    load_fen("7k/8/5KQ1/8/8/8/8/8 w - - 0 1");
    check(choose_move(&m, 3), "White tip finds mate in one");
    make_move(&m, &u);
    check(!generate_moves(0) && attacked(kings[1], WHITE), "tip delivers checkmate");
    unmake_move(&m, &u);
    check(search_score == MATE_SCORE - 1, "mate score and distance");
    load_fen("8/8/8/8/8/5kq1/8/7K b - - 0 1");
    check(choose_move(&m, 2), "Black CPU finds mate in one");
    make_move(&m, &u);
    check(!generate_moves(0) && attacked(kings[0], BLACK), "CPU delivers checkmate");
    unmake_move(&m, &u);
    load_fen("7k/5K2/6Q1/8/8/8/8/8 b - - 0 1");
    check(generate_moves(0) == 0 && !attacked(kings[1], WHITE), "stalemate");
    check(!choose_move(&m, 2), "no move in terminal position");

    for (depth = 1; depth <= 3; ++depth) {
        new_position();
        search_restoration(depth);
    }
    load_fen("r3k2r/8/8/3pP3/8/8/8/R3K2R w KQkq d6 17 1");
    search_restoration(2);
    load_fen("7k/P7/8/8/8/8/8/7K w - - 0 1");
    search_restoration(2);
    oracle_case("7k/8/5K2/8/3r4/8/3Q4/8 w - - 0 1", 2,
                "alpha-beta equals independent minimax oracle");
#ifndef _DCC_
    oracle_case("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2,
                "quiescence matches oracle in a tactical middlegame");
    oracle_case("r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP2BPPP/R1BQ1RK1 b - - 0 1", 2,
                "quiescence matches oracle in a quiet middlegame");
#endif
    oracle_case("8/2P5/8/8/8/5k2/1p6/K7 w - - 0 1", 2,
                "quiescence matches oracle with promotions");
    oracle_case("4k3/8/8/3pP3/8/8/8/4K2R w K d6 0 1", 3,
                "quiescence matches oracle with en passant and checks");
    position_key(before);
    test_abort = 1;
    choose_move(&m, 3);
    test_abort = 0;
    position_key(after);
    check(search_stopped && node_count <= 32
          && memcmp(before, after, POSITION_BYTES) == 0,
          "search cancellation restores position");
    node_count = NODE_LIMIT - 1;
    search_stopped = 0;
    minimax(1, 1, -INFINITY_SCORE, INFINITY_SCORE);
    position_key(after);
    check(search_stopped && node_count == NODE_LIMIT
          && memcmp(before, after, POSITION_BYTES) == 0,
          "exact node cap stops search without changing position");
    check(!choose_move(&m, 4) && engine_failed, "reject unsafe search depth");
    puts("CHESS minimax, tips and restoration PASS");
}

int main(void)
{
    perft_tests();
    rule_tests();
    eval_tests();
    search_tests();
    printf("CHESS tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
