#include <assert.h>

int bdos(int function, int argument);
#define main tictac_main
#include "../TICTAC.C"
#undef main

static int keys[8];
static int key_count, key_position;
static int timer_id, timer_polls, timer_starts, random_calls;

int bdos(int function, int argument)
{
    assert(function == 6 && argument == 255);
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
    return 0xffff;
}

static void empty_board(void)
{
    ginit();
    key_count = key_position = 0;
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
    for (row = 0; row < BSIZ; ++row)
        for (column = 0; column < BSIZ; ++column)
            if (row == 2 || column == 2 || row == column || row + column == 4)
                brd[row][column] = COMP;
    brd[2][2] = EMP;
    assert(aiscor(2, 2) == 80020L);
    aimove();
    assert(brd[2][2] == COMP);

    empty_board();
    assert(keymap(27) == K_NON && keymap('[') == K_NON);
    assert(keymap('A') == K_UP);
    assert(keymap(5) == K_UP && keymap(24) == K_DN);
    assert(keymap(19) == K_LT && keymap(4) == K_RT);
    assert(keymap(' ') == K_PLC && keymap('Q') == K_QUI);
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
    puts("TICTAC game logic PASS");
    return 0;
}
