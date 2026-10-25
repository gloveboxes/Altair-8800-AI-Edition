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
    return 0xffff;
}

static void empty_board(void)
{
    ginit();
    key_count = key_position = 0;
    console_buffer_used = output_used = input_calls = arrival_offset = 0;
    arriving_count = 0;
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
    console_tests();
    puts("TICTAC game logic PASS");
    puts("TICTAC raw console PASS");
    return 0;
}
