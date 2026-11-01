/*
 * CHESS - human (White) vs CPU (Black), dcc / CP/M 2.2.
 * Signed pieces on a 0x88 board; alpha-beta negamax is minimax with
 * scores measured from the side to move. All search storage is bounded.
 */
#include <stdlib.h>
#include <string.h>

#ifndef CHESS_TEST
#include "../../COMMON/DXSVC.H"
#endif

#define WHITE 1
#define BLACK (-1)
#define PAWN 1
#define KNIGHT 2
#define BISHOP 3
#define ROOK 4
#define QUEEN 5
#define KING 6
#define EP_CAPTURE 1
#define CASTLING 2
#define DOUBLE_PUSH 4
#define MAX_MOVES 256
#define MAX_DEPTH 5
#define QUIESCE_PLIES 4
#define MAX_PLY (MAX_DEPTH + QUIESCE_PLIES + 1)
#define MATE_SCORE 28000
#define INFINITY_SCORE 30000
#define NULL_REDUCTION 2
#define HANG_LOSS 200
#define TIP_MARGIN 150
#define OPENING_MOVES 10
#define KING_PENALTY 120
#define DEVELOP_BONUS 30
#define NODE_LIMIT 40000UL
#define POSITION_BYTES 35
#define HISTORY_SIZE 101

typedef struct {
    unsigned char from, to, promotion, flags;
} Move;

typedef struct {
    signed char piece, captured;
    unsigned char rights;
    int ep, clock, eval;
} Undo;

static signed char board[128];
static int side, ep_square, halfclock, eval_white;
static unsigned char castle_rights, kings[2];
static Move moves[MAX_PLY][MAX_MOVES];
static int move_count[MAX_PLY], order_score[MAX_PLY][MAX_MOVES];
static int first_legal_only, captures_only, gen_king, gen_in_check;
static unsigned char history[HISTORY_SIZE][POSITION_BYTES];
static int history_count;
static unsigned long node_count;
static int killers[MAX_PLY][2], just_nulled;
static int exclude_from = -1, exclude_to = -1, exclude_promotion;
static int full_moves, tip_bias_on, root_in_check;
static int search_stopped, engine_failed, completed_depth, search_score;
static const int piece_value[7] = {0, 100, 320, 330, 500, 900, 0};
static const int knight_steps[8] = {33, 31, 18, 14, -14, -18, -31, -33};
static const int ray_steps[8] = {16, -16, 1, -1, 17, 15, -15, -17};

#ifdef CHESS_TEST
static int test_abort;
static int poll_abort(void) { return test_abort; }
#else
static int poll_abort(void);
#endif

/* Valid for any square index in -128..255, which covers every ray and step. */
static inline int on_board(int square)
{
    return (square & 0x88) == 0;
}

static inline int iabs(int value)
{
    return value < 0 ? -value : value;
}

static inline int forward(int player)
{
    return player > 0 ? 16 : -16;
}

static inline int king_index(int player)
{
    return player == WHITE ? 0 : 1;
}

static int attacked(int square, int player)
{
    int i, s, p, step, pawn, knight, bishop, rook, queen, king, slider;

    if (player == WHITE) {
        pawn = PAWN; knight = KNIGHT; bishop = BISHOP;
        rook = ROOK; queen = QUEEN; king = KING;
        s = square - 17;
    } else {
        pawn = -PAWN; knight = -KNIGHT; bishop = -BISHOP;
        rook = -ROOK; queen = -QUEEN; king = -KING;
        s = square + 15;
    }
    if (on_board(s) && board[s] == pawn) return 1;
    s += 2;
    if (on_board(s) && board[s] == pawn) return 1;
    for (i = 0; i < 8; ++i) {
        s = square + knight_steps[i];
        if (on_board(s) && board[s] == knight) return 1;
        step = ray_steps[i];
        s = square + step;
        if (on_board(s) && board[s] == king) return 1;
        slider = i < 4 ? rook : bishop;
        while (on_board(s)) {
            p = board[s];
            if (p != 0) {
                if (p == queen || p == slider) return 1;
                break;
            }
            s += step;
        }
    }
    return 0;
}

static const signed char edge_distance[8] = {7, 5, 3, 1, 1, 3, 5, 7};

/* Material plus placement for one piece; positive for White, negative for Black. */
static int piece_eval(int piece, int square)
{
    int p, rank, file, center, value;

    p = piece < 0 ? -piece : piece;
    rank = square >> 4;
    file = square & 7;
    center = 14 - edge_distance[file] - edge_distance[rank];
    if (piece < 0) rank = 7 - rank;
    value = piece_value[p];
    if (p == PAWN) value += (rank << 3) + (center << 1);
    else if (p == KNIGHT) value += (center << 2) + center;
    else if (p == BISHOP) value += (center << 1) + center;
    else if (p == ROOK) {
        if (rank == 6) value += 20;
    } else if (p == KING && rank == 0 && (file == 2 || file == 6)) value += 35;
    return piece < 0 ? -value : value;
}

static void make_move(const Move *m, Undo *u)
{
    int from, to, flags, promotion, piece, captured, cap, rook_from, rook_to;
    int placed, rook;

    /* Scalar indices avoid dcc's nested member/index load miscompilation. */
    from = m->from;
    to = m->to;
    flags = m->flags;
    promotion = m->promotion;
    cap = (flags & EP_CAPTURE) ? to - forward(side) : to;
    piece = board[from];
    captured = board[cap];
    placed = promotion ? (side > 0 ? promotion : -promotion) : piece;
    u->piece = piece;
    u->captured = captured;
    u->ep = ep_square;
    u->clock = halfclock;
    u->rights = castle_rights;
    u->eval = eval_white;
    eval_white += piece_eval(placed, to) - piece_eval(piece, from);
    if (captured) eval_white -= piece_eval(captured, cap);
    board[from] = 0;
    board[cap] = 0;
    board[to] = placed;
    if (iabs(piece) == KING) {
        kings[king_index(side)] = to;
        castle_rights &= side == WHITE ? 12 : 3;
    }
    if (flags & CASTLING) {
        rook_from = to > from ? to + 1 : to - 2;
        rook_to = to > from ? to - 1 : to + 1;
        rook = board[rook_from];
        eval_white += piece_eval(rook, rook_to) - piece_eval(rook, rook_from);
        board[rook_to] = rook;
        board[rook_from] = 0;
    }
    if (from == 0 || to == 0) castle_rights &= 13;
    if (from == 7 || to == 7) castle_rights &= 14;
    if (from == 112 || to == 112) castle_rights &= 7;
    if (from == 119 || to == 119) castle_rights &= 11;
    ep_square = (flags & DOUBLE_PUSH) ? from + forward(side) : -1;
    halfclock = iabs(piece) == PAWN || captured ? 0 : halfclock + 1;
    side = -side;
}

static void unmake_move(const Move *m, const Undo *u)
{
    int from, to, flags, piece, captured, rook_from, rook_to;

    from = m->from;
    to = m->to;
    flags = m->flags;
    piece = u->piece;
    captured = u->captured;
    side = -side;
    board[from] = piece;
    board[to] = captured;
    if (flags & EP_CAPTURE) {
        board[to] = 0;
        board[to - forward(side)] = captured;
    }
    if (flags & CASTLING) {
        rook_from = to > from ? to + 1 : to - 2;
        rook_to = to > from ? to - 1 : to + 1;
        board[rook_from] = board[rook_to];
        board[rook_to] = 0;
    }
    if (iabs(piece) == KING) kings[king_index(side)] = from;
    castle_rights = u->rights;
    ep_square = u->ep;
    halfclock = u->clock;
    eval_white = u->eval;
}

/* True when two squares share a rank, file or diagonal. */
static inline int aligned(int a, int b)
{
    return (a >> 4) == (b >> 4) || (a & 7) == (b & 7)
        || (a >> 4) - (b >> 4) == (a & 7) - (b & 7)
        || (a >> 4) - (b >> 4) == (b & 7) - (a & 7);
}

static void add_move(int ply, int from, int to, int promotion, int flags)
{
    Move m;
    Undo u;
    int legal, player;

    if (first_legal_only && move_count[ply]) return;
    if (captures_only
        && (promotion ? promotion != QUEEN
            : board[to] == 0 && !(flags & EP_CAPTURE))) return;
    m.from = from;
    m.to = to;
    m.promotion = promotion;
    m.flags = flags;
    /* Only king moves, evasions, en passant and moves of a piece on a line
       with its king can leave that king attacked; skip the test otherwise. */
    if (gen_in_check || from == gen_king || (flags & EP_CAPTURE)
        || aligned(from, gen_king)) {
        player = side;
        make_move(&m, &u);
        legal = !attacked(kings[king_index(player)], -player);
        unmake_move(&m, &u);
        if (!legal) return;
    }
    /* A legal chess position has at most 218 legal moves. */
    if (move_count[ply] == MAX_MOVES) {
        engine_failed = 1;
        return;
    }
    memcpy(&moves[ply][move_count[ply]], &m, sizeof(Move));
    ++move_count[ply];
}

static void pawn_move(int ply, int from, int to, int flags)
{
    int p, rank;

    rank = to >> 4;
    if (rank == 0 || rank == 7) {
        for (p = QUEEN; p >= KNIGHT; --p)
            add_move(ply, from, to, p, flags);
    } else {
        add_move(ply, from, to, 0, flags);
    }
}

#define GEN_ALL 0
#define GEN_ONE 1
#define GEN_NOISY 2

/* GEN_ONE stops at the first legal move; GEN_NOISY lists only captures and
   queen promotions unless the king is in check, when every evasion is needed. */
static int generate_known(int ply, int mode, int in_check)
{
    int from, to, p, t, i, step, start, end, home, rights, rook;

    first_legal_only = mode == GEN_ONE;
    captures_only = mode == GEN_NOISY && !in_check;
    move_count[ply] = 0;
    gen_king = kings[king_index(side)];
    gen_in_check = in_check;
    rook = side > 0 ? ROOK : -ROOK;
    for (from = 0; from < 128; ++from) {
        if (first_legal_only && move_count[ply]) return move_count[ply];
        if (from & 0x88) {
            from += 7;
            continue;
        }
        p = board[from];
        if (side < 0) p = -p;
        if (p <= 0) continue;
        if (p == PAWN) {
            step = forward(side);
            to = from + step;
            if (on_board(to) && board[to] == 0
                && (!captures_only || (to >> 4) == 0 || (to >> 4) == 7)) {
                pawn_move(ply, from, to, 0);
                if ((from >> 4) == (side == WHITE ? 1 : 6)
                    && board[to + step] == 0)
                    add_move(ply, from, to + step, 0, DOUBLE_PUSH);
            }
            for (i = -1; i <= 1; i += 2) {
                to = from + step + i;
                if (!on_board(to)) continue;
                t = side > 0 ? board[to] : -board[to];
                if (t < 0 && t != -KING) pawn_move(ply, from, to, 0);
                if (to == ep_square && t == 0 && board[to - step] == -side)
                    add_move(ply, from, to, 0, EP_CAPTURE);
            }
        } else if (p == KNIGHT) {
            for (i = 0; i < 8; ++i) {
                to = from + knight_steps[i];
                if (!on_board(to)) continue;
                t = side > 0 ? board[to] : -board[to];
                if (t == 0 ? !captures_only : (t < 0 && t != -KING))
                    add_move(ply, from, to, 0, 0);
            }
        } else {
            start = p == BISHOP ? 4 : 0;
            end = p == ROOK ? 4 : 8;
            for (i = start; i < end; ++i) {
                step = ray_steps[i];
                to = from + step;
                while (on_board(to)) {
                    t = side > 0 ? board[to] : -board[to];
                    if (t > 0 || t == -KING) break;
                    if (t != 0 || !captures_only) add_move(ply, from, to, 0, 0);
                    if (t != 0 || p == KING) break;
                    to += step;
                }
            }
            if (p != KING) continue;
            home = side == WHITE ? 4 : 116;
            rights = side == WHITE ? castle_rights : castle_rights >> 2;
            if (from != home || gen_in_check || captures_only) continue;
            if ((rights & 1) && board[home + 3] == rook
                && board[home + 1] == 0 && board[home + 2] == 0
                && !attacked(home + 1, -side))
                add_move(ply, from, home + 2, 0, CASTLING);
            if ((rights & 2) && board[home - 4] == rook
                && board[home - 1] == 0 && board[home - 2] == 0
                && board[home - 3] == 0 && !attacked(home - 1, -side))
                add_move(ply, from, home - 2, 0, CASTLING);
        }
    }
    return move_count[ply];
}

static int generate(int ply, int mode)
{
    return generate_known(ply, mode, attacked(kings[king_index(side)], -side));
}

static int generate_moves(int ply)
{
    return generate(ply, GEN_ALL);
}

/* Repetition ignores an EP target unless an en-passant capture is legal. */
static int effective_ep(void)
{
    int i, from, player, legal;
    Move m;
    Undo u;

    if (ep_square < 0) return -1;
    player = side;
    for (i = -1; i <= 1; i += 2) {
        from = ep_square - forward(side) + i;
        if (!on_board(from) || board[from] != side) continue;
        m.from = from;
        m.to = ep_square;
        m.promotion = 0;
        m.flags = EP_CAPTURE;
        make_move(&m, &u);
        legal = !attacked(kings[king_index(player)], -player);
        unmake_move(&m, &u);
        if (legal) return ep_square;
    }
    return -1;
}

static void position_key(unsigned char *key)
{
    int r, f, p, n = 0, nibble;

    for (r = 0; r < 8; ++r)
        for (f = 0; f < 8; f += 2) {
            p = board[r * 16 + f];
            nibble = p < 0 ? 8 - p : p;
            p = board[r * 16 + f + 1];
            key[n++] = nibble | ((p < 0 ? 8 - p : p) << 4);
        }
    key[32] = side == WHITE;
    key[33] = castle_rights;
    key[34] = (unsigned char)effective_ep();
}

static void record_position(void)
{
    if (halfclock == 0) history_count = 0;
    if (history_count == HISTORY_SIZE) {
        engine_failed = 1;
        return;
    }
    position_key(history[history_count++]);
}

/* History holds the game and the root. Positions repeated only inside the
   search tree (at best the root's child and its great-grandchild) are not
   scored as draws; that is a deliberate saving on the Z80. */
static int repetitions(const unsigned char *key)
{
    int i, count = 0;

    for (i = 0; i < history_count; ++i)
        if (memcmp(key, history[i], POSITION_BYTES) == 0) ++count;
    return count;
}

static int insufficient_material(void)
{
    int s, p, minors = 0, knights = 0, bishop_colors = 0;

    for (s = 0; s < 128; ++s) {
        if (s & 0x88) {
            s += 7;
            continue;
        }
        p = iabs(board[s]);
        if (p == PAWN || p == ROOK || p == QUEEN) return 0;
        if (p == KNIGHT) {
            ++minors;
            ++knights;
        }
        if (p == BISHOP) {
            ++minors;
            bishop_colors |= 1 << (((s >> 4) + (s & 7)) & 1);
        }
    }
    return minors <= 1 || (!knights && bishop_colors != 3);
}

/* Full-board White-relative score; the search keeps eval_white incrementally. */
static int evaluate_white(void)
{
    int s, score = 0;

    for (s = 0; s < 128; ++s) {
        if (s & 0x88) {
            s += 7;
            continue;
        }
        if (board[s]) score += piece_eval(board[s], s);
    }
    return score;
}

static void order_moves(int ply)
{
    int i, j, value, captured;
    Move m;

    for (i = 0; i < move_count[ply]; ++i) {
        memcpy(&m, &moves[ply][i], sizeof(Move));
        captured = m.flags & EP_CAPTURE ? PAWN : iabs(board[m.to]);
        value = captured ? 10000 + piece_value[captured] * 10
            - piece_value[iabs(board[m.from])] : 0;
        if (m.promotion) value += 8000 + piece_value[m.promotion];
        if (m.flags & CASTLING) value += 100;
        if (value < 100) {
            j = m.from | (m.to << 8);
            if (j == killers[ply][0]) value = 5000;
            else if (j == killers[ply][1]) value = 4000;
        }
        j = i;
        while (j > 0 && order_score[ply][j - 1] < value) {
            memcpy(&moves[ply][j], &moves[ply][j - 1], sizeof(Move));
            order_score[ply][j] = order_score[ply][j - 1];
            --j;
        }
        memcpy(&moves[ply][j], &m, sizeof(Move));
        order_score[ply][j] = value;
    }
}

static int search_tick(void)
{
    ++node_count;
    if ((node_count & 31) == 0 && poll_abort()) search_stopped = 1;
    if (node_count >= NODE_LIMIT || engine_failed) search_stopped = 1;
    return search_stopped;
}

/* Resolves captures so the static score is not taken mid-exchange. The entry
   node was already counted and checked by minimax(). */
static int quiesce(int ply, int alpha, int beta, int qleft, int in_check)
{
    int i, score, best;
    Undo u;

    if (qleft < QUIESCE_PLIES) {
        if (search_tick()) return 0;
        if (halfclock == 0 && insufficient_material()) return 0;
    }
    best = side > 0 ? eval_white : -eval_white;
    if (qleft == 0) return best;
    if (!in_check) {
        if (best >= beta) return best;
        if (best > alpha) alpha = best;
    } else {
        best = -INFINITY_SCORE;
    }
    generate_known(ply, GEN_NOISY, in_check);
    if (move_count[ply] == 0)
        return in_check ? -MATE_SCORE + ply : best;
    order_moves(ply);
    for (i = 0; i < move_count[ply]; ++i) {
        make_move(&moves[ply][i], &u);
        score = -quiesce(ply + 1, -beta, -alpha, qleft - 1,
                         attacked(kings[king_index(side)], -side));
        unmake_move(&moves[ply][i], &u);
        if (search_stopped) return 0;
        if (score > best) best = score;
        if (score > alpha) alpha = score;
        if (alpha >= beta) break;
    }
    return best;
}

static int has_pieces(void)
{
    int s, p;

    for (s = 0; s < 128; ++s) {
        if (s & 0x88) {
            s += 7;
            continue;
        }
        p = board[s];
        if (side < 0) p = -p;
        if (p >= KNIGHT && p < KING) return 1;
    }
    return 0;
}

static int minimax(int depth, int ply, int alpha, int beta)
{
    int i, score, best = -INFINITY_SCORE, in_check, no_null, saved_ep;
    unsigned char key[POSITION_BYTES];
    Undo u;

    no_null = just_nulled;
    just_nulled = 0;
    if (search_tick()) return 0;
    /* At a leaf, one legal move is enough to rule out mate/stalemate. */
    if (generate(ply, depth == 0 ? GEN_ONE : GEN_ALL) == 0)
        return gen_in_check ? -MATE_SCORE + ply : 0;
    in_check = gen_in_check;
    if (halfclock >= 100) return 0;
    /* Material only changes on a capture or pawn move, and a search node
       can only repeat a position once four reversible plies have passed. */
    if ((halfclock == 0 || ply == 1) && insufficient_material()) return 0;
    if (halfclock >= 4) {
        position_key(key);
        if (repetitions(key) >= 2) return 0;
    }
    if (depth == 0) return quiesce(ply, alpha, beta, QUIESCE_PLIES, in_check);
    /* Pass the move: if even then the position beats beta, it will cut off.
       Skipped in check, after a pass, near mate scores and without pieces
       (zugzwang). Only reachable when depth >= 3, so depth <= 3 is exact. */
    if (depth > NULL_REDUCTION && !in_check && !no_null
        && beta < MATE_SCORE - MAX_PLY && has_pieces()) {
        saved_ep = ep_square;
        ep_square = -1;
        side = -side;
        just_nulled = 1;
        score = -minimax(depth - 1 - NULL_REDUCTION, ply + 1, -beta, -beta + 1);
        just_nulled = 0;
        side = -side;
        ep_square = saved_ep;
        if (search_stopped) return 0;
        if (score >= beta) return score >= MATE_SCORE - MAX_PLY ? beta : score;
    }
    order_moves(ply);
    for (i = 0; i < move_count[ply]; ++i) {
        make_move(&moves[ply][i], &u);
        score = -minimax(depth - 1, ply + 1, -beta, -alpha);
        unmake_move(&moves[ply][i], &u);
        if (search_stopped) return 0;
        if (score > best) best = score;
        if (score > alpha) alpha = score;
        if (alpha >= beta) {
            if (board[moves[ply][i].to] == 0 && moves[ply][i].promotion == 0
                && !(moves[ply][i].flags & EP_CAPTURE)) {
                killers[ply][1] = killers[ply][0];
                killers[ply][0] = moves[ply][i].from | (moves[ply][i].to << 8);
            }
            break;
        }
    }
    return best;
}

/* Tip-only root adjustment in the opening: early king walks are penalised
   (unless castling or escaping check); minor-piece development and central
   pawn pushes get a small bonus. */
static int opening_bias(const Move *m)
{
    int piece, home, from_rank;

    piece = iabs(board[m->from]);
    home = side == WHITE ? 0 : 7;
    from_rank = m->from >> 4;
    if (piece == KING) {
        if ((m->flags & CASTLING) || root_in_check) return 0;
        return -KING_PENALTY;
    }
    if ((piece == KNIGHT || piece == BISHOP) && from_rank == home
        && (m->to >> 4) != home) return DEVELOP_BONUS;
    if (piece == PAWN && board[m->to] == 0 && !(m->flags & EP_CAPTURE)
        && from_rank == (side == WHITE ? 1 : 6)
        && ((m->from & 7) == 3 || (m->from & 7) == 4)) return DEVELOP_BONUS;
    return 0;
}

static int choose_move(Move *choice, int max_depth)
{
    int depth, i, score, best, best_raw, bias;
    Move candidate;
    Undo u;

    node_count = 0;
    search_stopped = 0;
    completed_depth = 0;
    search_score = 0;
    just_nulled = 0;
    memset(killers, 0, sizeof(killers));
    if (max_depth < 1 || max_depth > MAX_DEPTH) {
        engine_failed = 1;
        return 0;
    }
    if (generate_moves(0) == 0) return 0;
    root_in_check = gen_in_check;
    eval_white = evaluate_white();
    order_moves(0);
    for (depth = 1; depth <= max_depth; ++depth) {
        best = -INFINITY_SCORE;
        best_raw = best;
        memcpy(&candidate, &moves[0][0], sizeof(Move));
        for (i = 0; i < move_count[0]; ++i) {
            if (moves[0][i].from == exclude_from && moves[0][i].to == exclude_to
                && moves[0][i].promotion == exclude_promotion) continue;
            bias = tip_bias_on ? opening_bias(&moves[0][i]) : 0;
            make_move(&moves[0][i], &u);
            score = -minimax(depth - 1, 1, -INFINITY_SCORE, bias - best);
            unmake_move(&moves[0][i], &u);
            if (search_stopped || engine_failed) break;
            if (score + bias > best) {
                best = score + bias;
                best_raw = score;
                memcpy(&candidate, &moves[0][i], sizeof(Move));
            }
        }
        if (search_stopped || engine_failed) break;
        memcpy(choice, &candidate, sizeof(Move));
        search_score = best_raw;
        completed_depth = depth;
        /* Search the previous iteration's best move first. */
        for (i = 0; i < move_count[0]; ++i)
            if (memcmp(&moves[0][i], &candidate, sizeof(Move)) == 0) {
                if (i != 0) {
                    memcpy(&moves[0][i], &moves[0][0], sizeof(Move));
                    memcpy(&moves[0][0], &candidate, sizeof(Move));
                }
                break;
            }
        if (best_raw >= MATE_SCORE - MAX_PLY) break;
    }
    return completed_depth > 0 && !engine_failed;
}

/* Search depth for the three UI levels: 1 = 3 plies, 2 = 4, 3 = 5. */
static inline int level_depth(int level)
{
    return level + 2;
}

/* Best net material the side to move can win by capturing on one square,
   when both sides may stop recapturing at any time. */
static int exchange_gain(int ply, int to, int limit)
{
    int i, value, best = 0;
    Undo u;

    if (limit == 0) return 0;
    generate_moves(ply);
    for (i = 0; i < move_count[ply]; ++i) {
        if (moves[ply][i].to != to || board[to] == 0) continue;
        value = piece_value[iabs(board[to])];
        make_move(&moves[ply][i], &u);
        value -= exchange_gain(ply + 1, to, limit - 1);
        unmake_move(&moves[ply][i], &u);
        if (value > best) best = value;
    }
    return best;
}

/* True when a knight, bishop, rook or queen moves onto a square where the
   opponent wins at least HANG_LOSS more material than the move captured. */
static int hangs_piece(const Move *m)
{
    int piece, captured, loss;
    Undo u;

    piece = iabs(board[m->from]);
    if (piece < KNIGHT || piece == KING) return 0;
    captured = piece_value[iabs(board[m->to])];
    make_move(m, &u);
    loss = exchange_gain(1, m->to, 4);
    unmake_move(m, &u);
    return loss - captured >= HANG_LOSS;
}

/* The tip is the search's best root move at the player's level. Unless that
   move mates, a piece-hanging choice is replaced by the best alternative
   when the alternative is within TIP_MARGIN of it and does not hang a piece. */
static int tip_search(Move *tip, int max_depth)
{
    Move first, alt;
    int first_score, first_depth, found;

    if (!choose_move(tip, max_depth)) return 0;
    if (search_score >= MATE_SCORE - MAX_PLY || !hangs_piece(tip)) return 1;
    memcpy(&first, tip, sizeof(Move));
    first_score = search_score;
    first_depth = completed_depth;
    exclude_from = first.from;
    exclude_to = first.to;
    exclude_promotion = first.promotion;
    found = choose_move(&alt, max_depth);
    exclude_from = exclude_to = -1;
    exclude_promotion = 0;
    if (found && first_score - search_score <= TIP_MARGIN && !hangs_piece(&alt)) {
        memcpy(tip, &alt, sizeof(Move));
        return 1;
    }
    memcpy(tip, &first, sizeof(Move));
    search_score = first_score;
    completed_depth = first_depth;
    return !engine_failed;
}

/* In the first OPENING_MOVES full moves the search also ranks root moves with
   opening_bias(); full_moves is 0 for positions set up outside a game. */
static int choose_tip(Move *tip, int max_depth)
{
    int ok;

    tip_bias_on = full_moves >= 1 && full_moves <= OPENING_MOVES;
    ok = tip_search(tip, max_depth);
    tip_bias_on = 0;
    return ok;
}

static void new_position(void)
{
    static const signed char back_rank[8] = {ROOK, KNIGHT, BISHOP, QUEEN,
                                            KING, BISHOP, KNIGHT, ROOK};
    int f;

    memset(board, 0, sizeof(board));
    for (f = 0; f < 8; ++f) {
        board[f] = back_rank[f];
        board[16 + f] = PAWN;
        board[96 + f] = -PAWN;
        board[112 + f] = -back_rank[f];
    }
    side = WHITE;
    kings[0] = 4;
    kings[1] = 116;
    castle_rights = 15;
    ep_square = -1;
    halfclock = 0;
    full_moves = 1;
    history_count = 0;
    engine_failed = 0;
    record_position();
}

#ifndef CHESS_TEST
#define CONSOLE_BUFFER_SIZE 2048
#define KEY_UP 256
#define KEY_DOWN 257
#define KEY_LEFT 258
#define KEY_RIGHT 259
#define KEY_ESCAPE 260
#define KEY_QUIT 261

static unsigned char console_buffer[CONSOLE_BUFFER_SIZE];
static unsigned console_used;
static int cursor_square, selected_square, hint_from, hint_to;
static unsigned char legal_targets[128];
static int level = 1, quit_requested, game_over, promotion_pending;
static int escape_state, wins, losses, draws;
static char last_move[8], tip_move[8];
static const char *message;

static void console_flush(void)
{
    unsigned i;

    for (i = 0; i < console_used; ++i)
        bios(4, console_buffer[i]);
    console_used = 0;
}

static inline void write_byte(unsigned char byte)
{
    if (console_used == CONSOLE_BUFFER_SIZE) console_flush();
    console_buffer[console_used++] = byte;
}

static void write_text(const char *text)
{
    unsigned char byte;

    while (*text) {
        byte = (unsigned char)*text++;
        write_byte(byte);
    }
}

static void write_number(int n)
{
    char digits[6];
    unsigned magnitude, count = 0;
    unsigned char byte;

    if (n < 0) write_byte('-');
    magnitude = n < 0 ? 0u - (unsigned)n : (unsigned)n;
    do {
        digits[count++] = '0' + magnitude % 10;
        magnitude /= 10;
    } while (magnitude);
    while (count) {
        byte = digits[--count];
        write_byte(byte);
    }
}

static void write_long(unsigned long n)
{
    char digits[10];
    unsigned count = 0;
    unsigned char byte;

    do {
        digits[count++] = '0' + (unsigned)(n % 10);
        n /= 10;
    } while (n);
    while (count) {
        byte = digits[--count];
        write_byte(byte);
    }
}

static void cursor(int row, int col)
{
    write_text("\033[");
    write_number(row);
    write_byte(';');
    write_number(col);
    write_byte('H');
}

static void color(int code)
{
    write_text("\033[");
    write_number(code);
    write_byte('m');
}

static void panel_line(int row, const char *text)
{
    cursor(row, 54);
    write_text("\033[0m                          ");
    cursor(row, 54);
    write_text(text);
}

static void move_text(const Move *m, char *text)
{
    static const char letters[] = " PNBRQK";

    text[0] = 'a' + m->from % 16;
    text[1] = '1' + m->from / 16;
    text[2] = '-';
    text[3] = 'a' + m->to % 16;
    text[4] = '1' + m->to / 16;
    text[5] = m->promotion ? letters[m->promotion] : ' ';
    text[6] = 0;
}

static void draw_cell(int square)
{
    static const char letters[] = " PNBRQK";
    static const char black_letters[] = " pnbrqk";
    int row, col, bg, p;

    row = 6 + (7 - square / 16) * 2;
    col = 8 + square % 16 * 5;
    bg = ((square / 16 + square % 16) & 1) ? 100 : 44;
    if (square == hint_from || square == hint_to) bg = 42;
    if (square == selected_square) bg = 43;
    if (square == cursor_square) bg = 46;
    cursor(row, col);
    color(bg);
    write_text("     ");
    cursor(row + 1, col);
    color(bg);
    p = board[square];
    color(p < 0 ? 91 : bg == 42 || bg == 43 || bg == 46 ? 30 : 97);
    write_byte(' ');
    write_byte(square == cursor_square ? '[' : ' ');
    if (p) write_byte(p > 0 ? letters[p] : black_letters[-p]);
    else write_byte(legal_targets[square] ? '.' : ' ');
    write_byte(square == cursor_square ? ']' : ' ');
    write_byte(' ');
    write_text("\033[0m");
}

static void draw_board(void)
{
    int r, f;

    for (r = 0; r < 8; ++r)
        for (f = 0; f < 8; ++f)
            draw_cell(r * 16 + f);
}

static void draw_panel(void)
{
    panel_line(6, "\033[36mHUMAN vs CPU");
    panel_line(8, "\033[97mWHITE: P N B R Q K");
    panel_line(9, "\033[91mBLACK: p n b r q k");
    panel_line(11, "LEVEL: ");
    write_number(level);
    if (level == 1) write_text("  Fast");
    else if (level == 2) write_text("  Standard");
    else write_text("  Hard");
    panel_line(12, "Minimax / alpha-beta");
    panel_line(14, side == WHITE ? "\033[36mYour turn (White)"
                               : "\033[33mCPU thinking...");
    if (game_over) panel_line(14, "GAME OVER - N to replay");
    panel_line(16, "LAST: ");
    write_text(last_move);
    panel_line(17, "TIP:  ");
    write_text(tip_move);
    panel_line(19, "Search depth: ");
    write_number(completed_depth);
    panel_line(20, "Nodes: ");
    write_long(node_count);
    panel_line(22, "W / L / D: ");
    write_number(wins);
    write_text(" / ");
    write_number(losses);
    write_text(" / ");
    write_number(draws);
    cursor(24, 3);
    write_text("\033[0m\033[K\033[33m");
    write_text(message);
    write_text("\033[0m");
    console_flush();
}

static void draw_frame(void)
{
    int r, c;

    write_text("\033[2J\033[H\033[?25l");
    cursor(1, 3);
    write_text("\033[36mCHESS\033[0m for \033[33mAltair 8800\033[0m  -  VT100");
    cursor(2, 3);
    write_text("Arrows Move  SPACE/ENTER Select & Move  T Tip  Q/Ctrl-C Quit");
    cursor(3, 3);
    write_text("1/2/3 Level  N New Game  ESC Cancel selection / Quit");
    for (r = 5; r <= 22; ++r) {
        cursor(r, 6);
        color((r / 2 & 1) ? 47 : 100);
        write_text("  ");
        cursor(r, 48);
        write_text("  \033[0m");
    }
    for (c = 0; c < 44; ++c) {
        cursor(5, 6 + c);
        color((c / 2 & 1) ? 47 : 100);
        write_byte(' ');
        cursor(22, 6 + c);
        write_byte(' ');
    }
    write_text("\033[0m");
    for (r = 0; r < 8; ++r) {
        cursor(7 + r * 2, 4);
        write_number(8 - r);
    }
    for (c = 0; c < 8; ++c) {
        cursor(23, 10 + c * 5);
        write_byte('a' + c);
    }
    draw_board();
    draw_panel();
}

static int read_action(void)
{
    int ch;

    ch = bdos(6, 255) & 255;
    if (!ch) {
        if (escape_state && !svc_timer_active(0)) {
            escape_state = 0;
            return KEY_ESCAPE;
        }
        return 0;
    }
    if (ch == 3 || ch == 'q' || ch == 'Q') {
        escape_state = 0;
        return KEY_QUIT;
    }
    if (escape_state == 2) {
        escape_state = 0;
        if (ch == 'A') return KEY_UP;
        if (ch == 'B') return KEY_DOWN;
        if (ch == 'C') return KEY_RIGHT;
        if (ch == 'D') return KEY_LEFT;
        return 0;
    }
    if (escape_state == 1) {
        escape_state = 0;
        if (ch == '[' || ch == 'O') {
            escape_state = 2;
            return 0;
        }
        return KEY_ESCAPE;
    }
    if (ch == 27) {
        escape_state = 1;
        svc_start_timer(0, 120);
        return 0;
    }
    if (ch == 5) return KEY_UP;
    if (ch == 24) return KEY_DOWN;
    if (ch == 19) return KEY_LEFT;
    if (ch == 4) return KEY_RIGHT;
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    return ch;
}

static int poll_abort(void)
{
    int action;

    action = read_action();
    if (action == KEY_QUIT || action == KEY_ESCAPE) quit_requested = 1;
    return quit_requested;
}

static void clear_selection(void)
{
    selected_square = hint_from = hint_to = -1;
    promotion_pending = 0;
    memset(legal_targets, 0, sizeof(legal_targets));
}

static void finish_turn(void)
{
    unsigned char key[POSITION_BYTES];
    int count;

    count = generate_moves(0);
    position_key(key);
    if (engine_failed) {
        message = "ERROR: chess engine storage exhausted. Q to quit.";
        game_over = 1;
    } else if (count == 0) {
        game_over = 1;
        if (attacked(kings[king_index(side)], -side)) {
            if (side == WHITE) {
                ++losses;
                message = "CHECKMATE - CPU wins! N for a new game.";
            } else {
                ++wins;
                message = "CHECKMATE - You win! N for a new game.";
            }
        } else {
            ++draws;
            message = "DRAW - Stalemate. N for a new game.";
        }
    } else if (halfclock >= 100 || insufficient_material()
               || repetitions(key) >= 3) {
        game_over = 1;
        ++draws;
        if (halfclock >= 100) message = "DRAW - Fifty-move rule. N for a new game.";
        else if (insufficient_material())
            message = "DRAW - Insufficient material. N for a new game.";
        else message = "DRAW - Threefold repetition. N for a new game.";
    } else if (attacked(kings[king_index(side)], -side)) {
        message = side == WHITE ? "CHECK! Select a White piece to defend your king."
                                : "CPU is in check.";
    } else {
        message = side == WHITE ? "Select a White piece, then a dotted destination."
                                : "CPU thinking... Q or ESC quits during search.";
    }
}

static void play_move(const Move *m)
{
    Undo u;

    move_text(m, last_move);
    if (side == BLACK) ++full_moves;
    make_move(m, &u);
    record_position();
    clear_selection();
    tip_move[0] = 0;
    finish_turn();
    draw_board();
    draw_panel();
}

static void show_tip(void)
{
    Move m;

    message = "Thinking about a tip... Q or ESC quits during search.";
    draw_panel();
    if (!choose_tip(&m, level_depth(level))) {
        if (!quit_requested) {
            message = engine_failed ? "ERROR: tip search failed." : "No legal tip.";
            draw_panel();
        }
        return;
    }
    if (quit_requested) return;
    clear_selection();
    hint_from = m.from;
    hint_to = m.to;
    cursor_square = m.from;
    move_text(&m, tip_move);
    message = "TIP: try the green squares. SPACE selects the source.";
    generate_moves(0);
    draw_board();
    draw_panel();
}

static void human_select(void)
{
    int i;
    Move m;

    if (selected_square < 0 || board[cursor_square] > 0) {
        if (board[cursor_square] <= 0) {
            message = "Select one of your White pieces first.";
        } else if (selected_square == cursor_square) {
            clear_selection();
            message = "Selection cancelled.";
        } else {
            clear_selection();
            selected_square = cursor_square;
            generate_moves(0);
            for (i = 0; i < move_count[0]; ++i)
                if (moves[0][i].from == selected_square)
                    legal_targets[moves[0][i].to] = 1;
            message = "Move to a dotted square and press SPACE or ENTER.";
        }
        draw_board();
        draw_panel();
        return;
    }
    for (i = 0; i < move_count[0]; ++i) {
        if (moves[0][i].from != selected_square
            || moves[0][i].to != cursor_square) continue;
        memcpy(&m, &moves[0][i], sizeof(Move));
        if (m.promotion) {
            promotion_pending = 1;
            message = "PROMOTE: 1 Queen  2 Rook  3 Bishop  4 Knight; ESC cancels.";
            draw_panel();
        } else {
            play_move(&m);
        }
        return;
    }
    message = "Illegal move. Choose a dotted destination.";
    draw_panel();
}

static void start_game(void)
{
    new_position();
    game_over = 0;
    cursor_square = 20;
    clear_selection();
    last_move[0] = tip_move[0] = 0;
    completed_depth = node_count = 0;
    finish_turn();
    draw_frame();
}

static void handle_action(int action)
{
    int old, next, step, i, promotion;
    Move m;

    if (action == KEY_QUIT) {
        quit_requested = 1;
        return;
    }
    if (action == KEY_ESCAPE) {
        if (selected_square < 0) {
            quit_requested = 1;
        } else {
            clear_selection();
            message = "Selection cancelled.";
            draw_board();
            draw_panel();
        }
        return;
    }
    if (promotion_pending) {
        promotion = action == '1' ? QUEEN : action == '2' ? ROOK
            : action == '3' ? BISHOP : action == '4' ? KNIGHT : 0;
        if (!promotion) {
            message = "PROMOTE: press 1, 2, 3 or 4; ESC cancels.";
            draw_panel();
            return;
        }
        for (i = 0; i < move_count[0]; ++i) {
            if (moves[0][i].from == selected_square
                && moves[0][i].to == cursor_square
                && moves[0][i].promotion == promotion) {
                memcpy(&m, &moves[0][i], sizeof(Move));
                play_move(&m);
                return;
            }
        }
        engine_failed = game_over = 1;
        message = "ERROR: promotion move missing. N to restart.";
        draw_panel();
        return;
    }
    if (action == 'N') {
        start_game();
        return;
    }
    if (action >= '1' && action <= '3') {
        level = action - '0';
        draw_panel();
        return;
    }
    if (game_over) return;
    if (action == 'T') {
        show_tip();
        return;
    }
    if (action == ' ' || action == 13 || action == 10) {
        human_select();
        return;
    }
    /* An if chain, not a nested ternary: dcc's peephole mis-schedules the
       cursor_square load for the ternary form once on_board is inlined. */
    if (action == KEY_UP) step = 16;
    else if (action == KEY_DOWN) step = -16;
    else if (action == KEY_LEFT) step = -1;
    else if (action == KEY_RIGHT) step = 1;
    else return;
    old = cursor_square;
    next = old + step;
    if (on_board(next)) {
        cursor_square = next;
        draw_cell(old);
        draw_cell(next);
        console_flush();
    }
}

int main(void)
{
    int action;
    Move m;

    start_game();
    while (!quit_requested) {
        if (side == BLACK && !game_over) {
            if (poll_abort()) break;
            draw_panel();
            if (choose_move(&m, level_depth(level)) && !quit_requested) {
                play_move(&m);
            } else if (!quit_requested) {
                game_over = 1;
                message = "ERROR: CPU search failed. N to restart or Q to quit.";
                draw_panel();
            }
        } else {
            action = read_action();
            if (action) handle_action(action);
        }
    }
    cursor(26, 1);
    write_text("\033[0m\033[?25hThanks for playing CHESS!\r\n");
    console_flush();
    return 0;
}
#endif
