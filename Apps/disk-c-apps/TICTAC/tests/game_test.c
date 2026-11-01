#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

int bdos(int function, int argument);
int bios(int function, int argument);
#define main tictac_main
#include "../TICTAC.C"
#undef main

static int keys[8];
static int key_count, key_position;
static int timer_id, timer_polls, timer_starts, random_calls;
static unsigned random_value = 0xffff;
static unsigned char output[32768];
static unsigned output_used, input_calls, arrival_offset;
static int arriving_keys[8], arriving_count;

int bios(int function, int argument)
{
    int index;

    assert(function == 4 && argument >= 0 && argument <= 255);
    assert(output_used < sizeof output);
    output[output_used++] = (unsigned char)argument;
    if (output_used == arrival_offset) {
        assert(key_count + arriving_count <= (int)(sizeof keys / sizeof keys[0]));
        for (index = 0; index < arriving_count; ++index)
            keys[key_count++] = arriving_keys[index];
    }
    return 0;
}

int bdos(int function, int argument)
{
    assert(function == 6 && argument == 255);
    ++input_calls;
    return key_position < key_count ? keys[key_position++] : 0;
}

int svc_start_timer(int id, unsigned duration)
{
    assert((id == 2 && duration == 25) || (id == 1 && duration == 400));
    timer_id = id;
    timer_polls = 0;
    ++timer_starts;
    return 0;
}

int svc_timer_active(int id)
{
    assert(id == timer_id);
    return timer_polls++ < 4;
}

unsigned svc_random_u16(void)
{
    ++random_calls;
    return random_value;
}

static void empty_board(void)
{
    ginit();
    key_count = key_position = 0;
    console_buffer_used = output_used = input_calls = arrival_offset = 0;
    arriving_count = 0;
    random_value = 0xffff;
}

static void opening_tests(void)
{
    int human, sample, row, column, count, index, expected, computers;
    int cells[9];

    for (human = 0; human < BSIZ * BSIZ; ++human) {
        count = 0;
        for (row = 1; row < BSIZ - 1; ++row)
            for (column = 1; column < BSIZ - 1; ++column)
                if (row * BSIZ + column != human)
                    cells[count++] = row * BSIZ + column;
        assert(count == 8 || count == 9);
        for (sample = 0; sample < count + 3; ++sample) {
            empty_board();
            brd[human / BSIZ][human % BSIZ] = HUM;
            random_value = sample < count ? (unsigned)sample :
                           sample == count ? 0x7fff :
                           sample == count + 1 ? 0x8000 : 0xffff;
            random_calls = 0;
            expected = cells[random_value % count];
            aimove();
            assert(random_calls == 1);
            for (index = 0; index < BSIZ * BSIZ; ++index)
                assert(brd[index / BSIZ][index % BSIZ] ==
                       (index == human ? HUM : index == expected ? COMP : EMP));
        }
    }

    empty_board();
    random_calls = 0;
    assert(aiopen() == -1 && random_calls == 0);
    brd[2][2] = HUM;
    random_value = 0;
    aimove();
    assert(brd[1][1] == COMP);
    brd[0][0] = HUM;
    random_calls = 0;
    assert(aiopen() == -1 && random_calls == 0);
    aimove();
    computers = 0;
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            computers += brd[row][column] == COMP;
    assert(computers == 2 && brd[1][1] == COMP && brd[0][0] == HUM);

    empty_board();
    brd[0][0] = brd[0][1] = HUM;
    random_calls = 0;
    assert(aiopen() == -1 && random_calls == 0);
}

static void draw_board(void)
{
    int row, column;

    empty_board();
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            brd[row][column] = (row + column / 2) % 2 ? COMP : HUM;
}

static int reference_score(void)
{
    static const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    static const int weights[4] = {0, 1, 10, 100};
    int row, column, direction, index, end_row, end_column, h, c;
    int score = 0, live = 0, value;

    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            for (direction = 0; direction < 4; ++direction) {
                end_row = row + (WLEN - 1) * directions[direction][0];
                end_column = column + (WLEN - 1) * directions[direction][1];
                if (end_row >= BSIZ || end_column < 0 || end_column >= BSIZ)
                    continue;
                h = c = 0;
                for (index = 0; index < WLEN; ++index) {
                    value = brd[row + index * directions[direction][0]]
                               [column + index * directions[direction][1]];
                    h += value == HUM;
                    c += value == COMP;
                }
                if (h == WLEN) return -30000;
                if (c == WLEN) return 30000;
                if (h && c) continue;
                live = 1;
                score += weights[c] - weights[h];
            }
    if (!live) return 31000;
    for (row = 1; row < BSIZ - 1; ++row)
        for (column = 1; column < BSIZ - 1; ++column) {
            value = row == 2 && column == 2 ? 20 : 5;
            if (brd[row][column] == COMP) score += value;
            if (brd[row][column] == HUM) score -= value;
        }
    return score;
}

/* Unpruned minimax with independent line geometry, not the production table. */
static int reference_search(int depth, int who)
{
    int row, column, value, best;

    value = reference_score();
    if (value == 30000) return value + depth;
    if (value == -30000) return value - depth;
    if (value == 31000) return 0;
    if (depth == 0) return value;
    best = who == COMP ? -32000 : 32000;
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column) {
            if (brd[row][column] != EMP) continue;
            brd[row][column] = who;
            value = reference_search(depth - 1, HUM + COMP - who);
            brd[row][column] = EMP;
            if (who == COMP && value > best) best = value;
            if (who == HUM && value < best) best = value;
        }
    return best;
}

static void check_search_position(void)
{
    int saved[BSIZ][BSIZ], row, column, expected, value, selected = -1;
    unsigned char saved_ai[BSIZ * BSIZ];
    int previous_random_calls = random_calls;

    memcpy(saved, brd, sizeof saved);
    aiprep(COMP);
    memcpy(saved_ai, aiboard, sizeof saved_ai);
    assert(aieval() == reference_score());
    expected = reference_search(AI_DEPTH, COMP);
    assert(memcmp(saved, brd, sizeof saved) == 0);
    assert(minimax(AI_DEPTH, COMP, -AI_INF, AI_INF) == expected);
    assert(memcmp(saved_ai, aiboard, sizeof saved_ai) == 0);
    assert(minimax(AI_DEPTH, COMP, expected - 1, expected + 1) == expected);
    assert(memcmp(saved_ai, aiboard, sizeof saved_ai) == 0);
    value = minimax(AI_DEPTH, COMP, expected + 1, AI_INF);
    assert(value >= expected && value <= expected + 1);
    assert(memcmp(saved_ai, aiboard, sizeof saved_ai) == 0);
    value = minimax(AI_DEPTH, COMP, -AI_INF, expected - 1);
    assert(value <= expected && value >= expected - 1);
    assert(memcmp(saved_ai, aiboard, sizeof saved_ai) == 0);
    assert(memcmp(saved, brd, sizeof saved) == 0);
    assert(random_calls == previous_random_calls && input_calls == 0);

    aimove();
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            if (saved[row][column] != brd[row][column]) {
                assert(selected == -1 && saved[row][column] == EMP);
                assert(brd[row][column] == COMP);
                selected = row * BSIZ + column;
            }
    assert(selected >= 0);
    assert(reference_search(AI_DEPTH - 1, HUM) == expected);
    assert(memcmp(saved_ai, aiboard, sizeof saved_ai) == 0);
}

static void check_tip_position(void)
{
    int saved[BSIZ][BSIZ], expected, cell;
    int wins = hwin, losses = cwin, draws = drws, previous_result = result;
    unsigned previous_input = input_calls;
    int previous_key = key_position;
    unsigned index;

    memcpy(saved, brd, sizeof saved);
    expected = reference_search(AI_DEPTH, HUM);
    dokey(keymap('t'));
    assert(memcmp(saved, brd, sizeof saved) == 0);
    assert(gstat == PLAY && turn == HUM && result == previous_result);
    assert(hwin == wins && cwin == losses && drws == draws);
    assert(input_calls == previous_input && key_position == previous_key);
    assert(tipactive && brd[crow][ccol] == EMP && console_buffer_used == 0);
    assert(output_used < sizeof output);
    output[output_used] = '\0';
    assert(strstr((const char *)output, "Finding tip...") != NULL);
    assert(strstr((const char *)output,
                  "Tip: highlighted square. SPACE to play.\033[0m") != NULL);
    for (index = 0; index < BSIZ * BSIZ; ++index)
        assert(aiboard[index] == brd[index / BSIZ][index % BSIZ]);
    cell = crow * BSIZ + ccol;
    brd[crow][ccol] = HUM;
    assert(reference_search(AI_DEPTH - 1, COMP) == expected);
    brd[cell / BSIZ][cell % BSIZ] = EMP;
}

static void minimax_tests(void)
{
    static const char *fork[BSIZ] = {"..XOX", "XXO..", ".X.XO", "X.O.O", "OO..X"};
    int row, column, cell, holes, sample, checked = 0, attempt, moves, selected;
    int cells[BSIZ * BSIZ], saved[BSIZ][BSIZ];
    unsigned long state = 27;

    assert(AI_DEPTH == 2);
    empty_board();
    brd[2][2] = brd[1][2] = HUM;
    brd[3][3] = COMP;
    check_search_position();
    empty_board();
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            brd[row][column] = fork[row][column] == 'X' ? HUM :
                               fork[row][column] == 'O' ? COMP : EMP;
    assert(reference_search(AI_DEPTH, COMP) == 9);
    check_search_position();
    assert(brd[2][2] == COMP && !chkwin(COMP));

    for (attempt = 0; attempt < 1000 && checked < 32; ++attempt) {
        empty_board();
        holes = BSIZ * BSIZ;
        for (cell = 0; cell < holes; ++cell) cells[cell] = cell;
        moves = 9 + 2 * (checked % 6);
        for (sample = 0; sample < moves; ++sample) {
            state = (state * 1664525UL + 1013904223UL) & 0xffffffffUL;
            cell = (int)(state % (unsigned)holes);
            row = cells[cell] / BSIZ;
            column = cells[cell] % BSIZ;
            brd[row][column] = sample % 2 ? COMP : HUM;
            cells[cell] = cells[--holes];
        }
        if (chkwin(HUM) || chkwin(COMP) || !winleft()) continue;
        check_search_position();
        if (!chkwin(COMP) && !chkwin(HUM) && winleft())
            check_tip_position();
        ++checked;
    }
    assert(checked == 32);

    draw_board();
    for (column = 0; column < BSIZ; ++column)
        brd[0][column] = column == 1 ? HUM : EMP;
    memcpy(saved, brd, sizeof saved);
    holes = 0;
    for (sample = 0; sample < 4; ++sample) {
        empty_board();
        memcpy(brd, saved, sizeof saved);
        random_value = (unsigned)sample;
        random_calls = 0;
        aimove();
        selected = -1;
        for (cell = 0; cell < BSIZ * BSIZ; ++cell)
            if (brd[cell / BSIZ][cell % BSIZ] != saved[cell / BSIZ][cell % BSIZ]) {
                assert(selected == -1 && brd[cell / BSIZ][cell % BSIZ] == COMP);
                selected = cell;
            }
        assert(selected == 0 || selected == 2 || selected == 3 || selected == 4);
        assert(random_calls == 1 && reference_search(AI_DEPTH - 1, HUM) == 0);
        holes |= 1 << selected;
    }
    assert(holes == ((1 << 0) | (1 << 2) | (1 << 3) | (1 << 4)));

    draw_board();
    brd[0][0] = EMP;
    aiprep(COMP);
    assert(aieval() == AI_DRAW && minimax(AI_DEPTH, COMP, -AI_INF, AI_INF) == 0);
    draw_board();
    memcpy(saved, brd, sizeof saved);
    aimove();
    assert(gstat == DONE && result == 3 && memcmp(saved, brd, sizeof saved) == 0);
    puts("TICTAC two-ply minimax, independent oracle, tactics and restoration PASS");
}

static void tip_tests(void)
{
    static const char *fork[BSIZ] = {"..XOX", "XXO..", ".X.XO", "X.O.O", "OO..X"};
    int saved[BSIZ][BSIZ], row, column, old_row, old_column;

    empty_board();
    check_tip_position();
    assert(crow == 2 && ccol == 2);
    dokey(keymap('T'));
    assert(tipactive && !chkful() && turn == HUM && gstat == PLAY);
    old_row = crow;
    old_column = ccol;
    output_used = 0;
    dokey(K_RT);
    console_flush();
    assert(!tipactive && crow == old_row && ccol == old_column + 1);
    assert(memcmp(output, "\033[26;18H\033[K", 11) == 0);
    ginit();
    assert(!tipactive);

    empty_board();
    for (column = 0; column < 3; ++column) {
        brd[0][column] = HUM;
        brd[1][column] = COMP;
    }
    check_tip_position();
    assert(crow == 0 && ccol == 3);
    dokey(K_PLC);
    assert(!tipactive && brd[0][3] == HUM && gstat == DONE && result == 1);

    empty_board();
    for (column = 0; column < 3; ++column) brd[0][column] = COMP;
    brd[1][0] = brd[1][1] = brd[4][4] = HUM;
    check_tip_position();
    assert(crow == 0 && ccol == 3);

    empty_board();
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            brd[row][column] = fork[row][column] == 'X' ? COMP :
                               fork[row][column] == 'O' ? HUM : EMP;
    brd[4][4] = EMP;
    assert(reference_search(AI_DEPTH, HUM) == -20);
    check_tip_position();
    assert(crow == 2 && ccol == 2);

    empty_board();
    for (column = 0; column < 3; ++column) brd[0][column] = HUM;
    keys[0] = 'T';
    keys[1] = ' ';
    key_count = 2;
    timer_starts = 0;
    hmturn();
    assert(key_position == 2 && timer_starts == 1);
    assert(brd[0][3] == HUM && gstat == DONE && result == 1 && !tipactive);

    empty_board();
    turn = COMP;
    memcpy(saved, brd, sizeof saved);
    dokey(K_TIP);
    output[output_used] = '\0';
    assert(!tipactive && turn == COMP && gstat == PLAY);
    assert(memcmp(saved, brd, sizeof saved) == 0);
    assert(strstr((const char *)output, "Tips are only available on your turn.") != NULL);
    draw_board();
    memcpy(saved, brd, sizeof saved);
    dokey(K_TIP);
    output[output_used] = '\0';
    assert(!tipactive && memcmp(saved, brd, sizeof saved) == 0);
    assert(strstr((const char *)output, "No legal moves available.") != NULL);
    puts("TICTAC T/t tips, human minimax oracle, no moves/scores changed and SPACE/clear/reset PASS");
}

static int reference_winleft(void)
{
    static const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    int row, column, direction, index, end_row, end_column, pieces;

    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            for (direction = 0; direction < 4; ++direction) {
                end_row = row + (WLEN - 1) * directions[direction][0];
                end_column = column + (WLEN - 1) * directions[direction][1];
                if (end_row >= BSIZ || end_column < 0 || end_column >= BSIZ)
                    continue;
                pieces = 0;
                for (index = 0; index < WLEN; ++index)
                    pieces |= brd[row + index * directions[direction][0]]
                                 [column + index * directions[direction][1]];
                if (pieces != (HUM | COMP))
                    return 1;
            }
    return 0;
}

static void winning_line_tests(void)
{
    static const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    int row, column, direction, pattern, value, index, end_row, end_column;
    int lines = 0;

    empty_board();
    assert(winleft() && !chkwin(HUM) && !chkwin(COMP));
    draw_board();
    assert(!winleft() && !chkwin(HUM) && !chkwin(COMP));
    brd[0][0] = EMP;
    assert(!winleft() && !chkful());

    /* Exercise all 81 piece/empty patterns on each of the 28 winning lines. */
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            for (direction = 0; direction < 4; ++direction) {
                end_row = row + (WLEN - 1) * directions[direction][0];
                end_column = column + (WLEN - 1) * directions[direction][1];
                if (end_row >= BSIZ || end_column < 0 || end_column >= BSIZ)
                    continue;
                ++lines;
                for (pattern = 0; pattern < 81; ++pattern) {
                    draw_board();
                    value = pattern;
                    for (index = 0; index < WLEN; ++index) {
                        brd[row + index * directions[direction][0]]
                           [column + index * directions[direction][1]] = value % 3;
                        value /= 3;
                    }
                    assert(winleft() == reference_winleft());
                }
            }
    assert(lines == 28);
}

static void early_draw_tests(void)
{
    int column;

    draw_board();
    for (column = 0; column < 3; ++column) brd[0][column] = EMP;
    assert(winleft() && !chkwin(HUM) && !chkwin(COMP));
    crow = ccol = 0;
    keys[0] = ' ';
    keys[1] = 4;
    key_count = 2;
    timer_starts = random_calls = 0;
    hmturn();
    assert(gstat == DONE && result == 4 && turn == HUM);
    assert(brd[0][0] == HUM && !winleft() && !chkful());
    assert(key_position == 1 && timer_starts == 1 && random_calls == 0);
    hwin = cwin = drws = 0;
    overdr();
    console_flush();
    output[output_used] = '\0';
    assert(strstr((const char *)output,
                  "\033[26;18H\033[5mNo winning moves left.\033[0m") != NULL);
    assert(strstr((const char *)output, "DRAW!") != NULL);
    assert(hwin == 0 && cwin == 0 && drws == 1);
    ginit();
    assert(gstat == PLAY && result == 0 && winleft() && drws == 1);

    draw_board();
    for (column = 0; column < BSIZ; ++column)
        brd[0][column] = column == 1 ? HUM : EMP;
    assert(winleft() && !chkwin(HUM) && !chkwin(COMP));
    turn = COMP;
    random_value = 2;
    cpturn();
    assert(gstat == DONE && result == 4 && turn == COMP);
    assert(brd[0][2] == COMP && !winleft() && !chkful());

    empty_board();
    for (column = 0; column < WLEN - 1; ++column) brd[0][column] = HUM;
    crow = 0;
    ccol = WLEN - 1;
    dokey(K_PLC);
    assert(gstat == DONE && result == 1 && chkwin(HUM));

    empty_board();
    for (column = 0; column < WLEN - 1; ++column) brd[0][column] = COMP;
    turn = COMP;
    cpturn();
    assert(gstat == DONE && result == 2 && chkwin(COMP));

    draw_board();
    crow = ccol = BSIZ - 1;
    brd[crow][ccol] = EMP;
    dokey(K_PLC);
    assert(gstat == DONE && result == 3 && chkful());
    overdr();
    console_flush();
    output[output_used] = '\0';
    assert(strstr((const char *)output, "No winning moves left.") == NULL);
    assert(strstr((const char *)output, "\033[5m") == NULL);

    draw_board();
    brd[0][2] = EMP;
    turn = COMP;
    cpturn();
    assert(gstat == DONE && result == 3 && chkful());
}

static void console_tests(void)
{
    static const unsigned lengths[] = {
        0, 1, CONSOLE_BUFFER_SIZE - 1, CONSOLE_BUFFER_SIZE,
        CONSOLE_BUFFER_SIZE + 1, 2 * CONSOLE_BUFFER_SIZE + 17
    };
    static const char rendering[] =
        "\033[5;18H\033[107m$\r\n0 32767 -32768\033[0m";
    static const char suffix[] = "\033[31m$-32768";
    static const char ending[] =
        "\033[28;1H\033[?25h\033[0mThanks for playing!\r\n";
    unsigned test, index, length;

    for (test = 0; test < sizeof lengths / sizeof lengths[0]; ++test) {
        empty_board();
        length = lengths[test];
        for (index = 0; index < length; ++index) {
            console_put_byte((unsigned char)index);
            assert(console_buffer_used <= CONSOLE_BUFFER_SIZE);
        }
        assert(output_used == (length == 0 ? 0 :
               ((length - 1) / CONSOLE_BUFFER_SIZE) * CONSOLE_BUFFER_SIZE));
        console_flush();
        assert(output_used == length && console_buffer_used == 0);
        for (index = 0; index < length; ++index)
            assert(output[index] == (unsigned char)index);
        console_flush();
        assert(output_used == length && input_calls == 0);
    }

    empty_board();
    for (index = 0; index < CONSOLE_BUFFER_SIZE - 1; ++index)
        console_put_byte('.');
    write_text("\033[31m$");
    write_int(-32768);
    assert(output_used == CONSOLE_BUFFER_SIZE);
    console_flush();
    assert(output_used == CONSOLE_BUFFER_SIZE - 1 + sizeof suffix - 1);
    for (index = 0; index < CONSOLE_BUFFER_SIZE - 1; ++index)
        assert(output[index] == '.');
    assert(memcmp(output + CONSOLE_BUFFER_SIZE - 1,
                  suffix, sizeof suffix - 1) == 0);

    empty_board();
    cursor(5, 18);
    color(107);
    pstr("$\r\n");
    write_int(0);
    console_put_byte(' ');
    write_int(32767);
    console_put_byte(' ');
    write_int(-32768);
    reset_color();
    assert(output_used == 0);
    console_flush();
    assert(output_used == sizeof rendering - 1);
    assert(memcmp(output, rendering, sizeof rendering - 1) == 0);

    empty_board();
    arriving_keys[0] = 19;
    arriving_keys[1] = 4;
    arriving_count = 2;
    arrival_offset = 100;
    border();
    drwbrd();
    console_flush();
    assert(output_used > CONSOLE_BUFFER_SIZE);
    assert(key_count == 2 && key_position == 0 && input_calls == 0);
    dokey(keymap(read_key()));
    assert(ccol == 1 && crow == 2);
    dokey(keymap(read_key()));
    assert(ccol == 2 && crow == 2 && gstat == PLAY);

    empty_board();
    arriving_keys[0] = 19;
    arriving_keys[1] = 4;
    arriving_count = 2;
    arrival_offset = CONSOLE_BUFFER_SIZE / 2;
    for (index = 0; index < CONSOLE_BUFFER_SIZE + 1; ++index)
        console_put_byte(255);
    console_flush();
    assert(output_used == CONSOLE_BUFFER_SIZE + 1);
    assert(key_position == 0 && input_calls == 0);
    assert(read_key() == 19 && read_key() == 4 && read_key() == 0);

    empty_board();
    arriving_keys[0] = 27;
    arriving_keys[1] = '[';
    arriving_keys[2] = 'C';
    arriving_count = 3;
    arrival_offset = 2;
    drwcel(crow, ccol, 1);
    console_flush();
    for (index = 0; index < 3; ++index)
        dokey(keymap(read_key()));
    assert(ccol == 3 && escst == 0 && gstat == PLAY);

    hwin = cwin = drws = 0;
    for (test = 0; test < 3; ++test) {
        empty_board();
        result = (int)test + 1;
        arriving_keys[0] = test == 0 ? 'Y' : test == 1 ? 'N' : 27;
        arriving_count = 1;
        arrival_offset = 1;
        assert(replay() == (test == 0));
        assert(key_position == 1 && console_buffer_used == 0);
        assert(output_used < sizeof output);
        output[output_used] = '\0';
        assert(strstr((const char *)output, "Play again? (Y/N)") != NULL);
    }
    assert(hwin == 1 && cwin == 1 && drws == 1);

    for (test = 0; test < 2; ++test) {
        empty_board();
        timer_starts = 0;
        arriving_keys[0] = test == 0 ? 'q' : 27;
        arriving_count = 1;
        arrival_offset = 100;
        assert(tictac_main() == 0);
        assert(gstat == QUIT && key_position == 1);
        assert(timer_starts == (test == 0 ? 1 : 3));
        assert(console_buffer_used == 0 && output_used > sizeof ending - 1);
        assert(memcmp(output + output_used - (sizeof ending - 1),
                      ending, sizeof ending - 1) == 0);
    }
}

int main(void)
{
    int row, column;
    empty_board();
    keys[0] = ' ';
    keys[1] = 4;
    key_count = 2;
    hmturn();
    assert(brd[2][2] == HUM && turn == COMP);
    assert(key_position == 1);
    cpturn();
    assert(turn == HUM && gstat == PLAY);
    assert(timer_starts == 2 && timer_id == 1 && timer_polls == 5);
    assert(random_calls > 0);
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            if (brd[row][column] == COMP) goto found_cpu;
    assert(0);
found_cpu:
    empty_board();
    for (column = 0; column < 3; ++column) brd[0][column] = COMP;
    aimove();
    assert(brd[0][3] == COMP && chkwin(COMP));
    empty_board();
    for (column = 0; column < 3; ++column) brd[0][column] = HUM;
    aimove();
    assert(brd[0][3] == COMP && !chkwin(HUM));

    empty_board();
    assert(keymap(27) == K_NON && keymap('[') == K_NON);
    assert(keymap('A') == K_UP);
    assert(keymap(5) == K_UP && keymap(24) == K_DN);
    assert(keymap(19) == K_LT && keymap(4) == K_RT);
    assert(keymap(' ') == K_PLC && keymap('Q') == K_QUI);
    assert(keymap('t') == K_TIP && keymap('T') == K_TIP);
    keymap(27);
    esctik(); esctik(); esctik();
    assert(gstat == QUIT);
    empty_board();
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            brd[row][column] = HUM;
    assert(chkful());
    brd[4][4] = EMP;
    assert(!chkful());
    winning_line_tests();
    early_draw_tests();
    opening_tests();
    minimax_tests();
    tip_tests();
    console_tests();
    puts("TICTAC game logic PASS");
    puts("TICTAC raw console PASS");
    return 0;
}
