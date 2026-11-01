#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <vector>
extern "C" {
#include "memory.h"
#include "z80.h"
}
#include "x80.hxx"

static z80_t cpu;
static uint8_t no_in() { return 0; }
static void no_out(uint8_t) {}
static disk_controller_t disk = {no_out, no_in, no_out, no_in, no_out, no_in};
static unsigned timer_command, parameter_count, duration;
static unsigned polls[4], starts[4], service_status, request_length;
static unsigned response_position, random_calls;
static uint16_t random_value = 0xffff;
static std::vector<uint8_t> request;
static std::string output;
static bool placed, quit;
static unsigned arrow_position;
static std::vector<uint8_t> drawing_keys;
static unsigned drawing_key_position;

static uint8_t input(uint8_t port)
{
    if (port == 23) {
        assert(timer_command >= 0x20 && timer_command <= 0x23);
        unsigned id = timer_command - 0x20;
        return polls[id]++ < 3;
    }
    if (port == 2) return service_status;
    assert(port == 4 && service_status == 2);
    const uint8_t response[] = {2, 0, uint8_t(random_value), uint8_t(random_value >> 8)};
    uint8_t byte = response[response_position++];
    if (response_position == sizeof(response)) service_status = 0;
    return byte;
}

static void out(uint8_t port, uint8_t byte)
{
    if (port == 23) {
        if (parameter_count) {
            if (parameter_count == 2) duration = byte;
            else duration |= unsigned(byte) << 8;
            if (--parameter_count == 0) {
                unsigned id = timer_command - 0x10;
                assert((id == 1 && duration == 400) || (id == 2 && duration == 25));
                ++starts[id];
                polls[id] = 0;
            }
        } else {
            timer_command = byte;
            if (byte >= 0x10 && byte <= 0x12) parameter_count = 2;
            else assert(byte >= 0x20 && byte <= 0x23);
        }
        return;
    }
    if (port == 2) {
        assert(service_status == 0 && byte == 0x21);
        request.clear();
        service_status = 3;
        response_position = 0;
        return;
    }
    assert(port == 3 && service_status == 3);
    request.push_back(byte);
    if (request.size() == 2) request_length = request[0] + 256 * request[1];
    if (request.size() == request_length + 2) {
        assert(request == std::vector<uint8_t>({1, 0, 0}));
        service_status = 2;
        ++random_calls;
    }
}

static unsigned call_guest(unsigned address, const std::vector<uint16_t> &arguments,
                           unsigned limit = 1000000, unsigned *stack_used = nullptr)
{
    reg.sp = 0xe000;
    memory[reg.sp] = 0xf0;
    memory[reg.sp + 1] = 0xff;
    for (unsigned index = 0; index < arguments.size(); ++index) {
        memory[reg.sp + 2 + 2 * index] = arguments[index] & 255;
        memory[reg.sp + 3 + 2 * index] = arguments[index] >> 8;
    }
    reg.pc = address;
    unsigned steps = 0, lowest_sp = reg.sp;
    for (; reg.pc != 0xfff0; ++steps) {
        assert(steps < limit);
        if (reg.sp < lowest_sp) lowest_sp = reg.sp;
        if (reg.pc == 0xf10c) {
            output += char(reg.c);
            reg.pc = memory[reg.sp] + 256 * memory[reg.sp + 1];
            reg.sp += 2;
        } else {
            assert(reg.pc != 0 && reg.pc != 5);
            x80_emulate(1);
        }
    }
    if (stack_used) *stack_used = 0xe000 - lowest_sp;
    return steps;
}

static void console_tests(const std::map<std::string, unsigned> &addresses)
{
    const unsigned text_address = 0xc000;
    auto call = [&](const char *name, const std::vector<uint16_t> &arguments) {
        call_guest(addresses.at(std::string("TICTAC:") + name), arguments);
    };
    auto write = [&](const std::string &text) {
        std::memcpy(memory + text_address, text.c_str(), text.size() + 1);
        call("write_text", {uint16_t(text_address)});
    };
    call("cursor", {5, 18});
    call("color", {107});
    write("$\r\n");
    call("write_int", {0});
    write(" ");
    call("write_int", {32767});
    write(" ");
    call("write_int", {0x8000});
    call("reset_color", {});
    assert(output.empty());
    call("console_flush", {});
    assert(output == "\033[5;18H\033[107m$\r\n0 32767 -32768\033[0m");
    output.clear();

    write(std::string(2047, '.'));
    call("color", {31});
    write("$");
    call("write_int", {0x8000});
    call("console_flush", {});
    assert(output == std::string(2047, '.') + "\033[31m$-32768");
    output.clear();

    write(std::string(2048, '.'));
    call("write_int", {0x8000});
    call("console_flush", {});
    assert(output == std::string(2048, '.') + "-32768");
    output.clear();
    puts("Z80 exact ANSI/decimal formatting and buffer-boundary output PASS");
}

static void opening_tests(const std::map<std::string, unsigned> &addresses)
{
    unsigned board = addresses.at("TICTAC:_BRD");
    for (unsigned human = 0; human < 25; ++human) {
        std::vector<unsigned> cells;
        for (unsigned row = 1; row < 4; ++row)
            for (unsigned column = 1; column < 4; ++column)
                if (row * 5 + column != human)
                    cells.push_back(row * 5 + column);
        assert(cells.size() == 8 || cells.size() == 9);
        for (unsigned sample = 0; sample < cells.size() + 3; ++sample) {
            call_guest(addresses.at("_GINIT"), {});
            memory[board + 2 * human] = 1;
            random_value = sample < cells.size() ? sample :
                           sample == cells.size() ? 0x7fff :
                           sample == cells.size() + 1 ? 0x8000 : 0xffff;
            unsigned previous_calls = random_calls;
            unsigned expected = cells[random_value % cells.size()];
            call_guest(addresses.at("_AIMOV"), {});
            assert(random_calls == previous_calls + 1);
            for (unsigned index = 0; index < 25; ++index) {
                assert(memory[board + 2 * index] ==
                       (index == human ? 1 : index == expected ? 2 : 0));
                assert(memory[board + 2 * index + 1] == 0);
            }
            call_guest(addresses.at("TICTAC:console_flush"), {});
            output.clear();
        }
    }
    random_value = 0xffff;
    puts("Z80 random central openings for every human first square and RNG boundary values PASS");
}

static void early_draw_tests(const std::map<std::string, unsigned> &addresses)
{
    unsigned board = addresses.at("TICTAC:_BRD");
    auto store = [&](unsigned address, uint16_t value) {
        memory[address] = value & 255;
        memory[address + 1] = value >> 8;
    };
    auto get = [&](const char *name) {
        unsigned address = addresses.at(std::string("TICTAC:_") + name);
        return memory[address] + 256 * memory[address + 1];
    };
    auto set = [&](const char *name, uint16_t value) {
        store(addresses.at(std::string("TICTAC:_") + name), value);
    };
    auto call = [&](const char *name, const std::vector<uint16_t> &arguments) {
        call_guest(addresses.at(name), arguments);
    };
    auto draw_board = [&]() {
        call("_GINIT", {});
        for (unsigned row = 0; row < 5; ++row)
            for (unsigned column = 0; column < 5; ++column)
                store(board + 2 * (row * 5 + column),
                      (row + column / 2) % 2 ? 2 : 1);
        output.clear();
    };

    draw_board();
    call("TICTAC:winleft", {});
    assert(reg.H() == 0);
    for (unsigned column = 0; column < 3; ++column)
        store(board + 2 * column, 0);
    call("TICTAC:winleft", {});
    assert(reg.H() == 1);
    set("CROW", 0);
    set("CCOL", 0);
    call("_DOKEY", {5});
    assert(get("GSTAT") == 1 && get("RESULT") == 4 && get("TURN") == 1);
    assert(memory[board] == 1 && memory[board + 2] == 0 && memory[board + 4] == 0);
    set("HWIN", 0);
    set("CWIN", 0);
    set("DRWS", 0);
    call("_OVERD", {});
    call("TICTAC:console_flush", {});
    assert(get("HWIN") == 0 && get("CWIN") == 0 && get("DRWS") == 1);
    assert(output.find("\033[26;18H\033[5mNo winning moves left.\033[0m") !=
           std::string::npos);
    call("_GINIT", {});
    assert(get("GSTAT") == 0 && get("RESULT") == 0 && get("DRWS") == 1);
    call("TICTAC:winleft", {});
    assert(reg.H() == 1);

    draw_board();
    for (unsigned column = 0; column < 5; ++column)
        store(board + 2 * column, column == 1 ? 1 : 0);
    set("TURN", 2);
    random_value = 2;
    call("_CPTUR", {});
    random_value = 0xffff;
    assert(get("GSTAT") == 1 && get("RESULT") == 4 && get("TURN") == 2);
    assert(memory[board + 4] == 2 && memory[board] == 0);
    call("TICTAC:winleft", {});
    assert(reg.H() == 0);
    call("TICTAC:console_flush", {});
    output.clear();
    puts("Z80 early draws after human/CPU moves, message, scoring and reset PASS");
}

static void tip_tests(const std::map<std::string, unsigned> &addresses)
{
    const char *fork[] = {"..XOX", "XXO..", ".X.XO", "X.O.O", "OO..X"};
    unsigned board = addresses.at("TICTAC:_BRD");
    auto get = [&](const char *name) {
        unsigned address = addresses.at(std::string("TICTAC:_") + name);
        return memory[address] + 256 * memory[address + 1];
    };
    auto reset = [&]() {
        call_guest(addresses.at("_GINIT"), {});
        output.clear();
    };
    auto set = [&](unsigned cell, uint8_t piece) {
        memory[board + 2 * cell] = piece;
        memory[board + 2 * cell + 1] = 0;
    };
    auto hint = [&](uint16_t letter, unsigned expected) {
        std::vector<uint8_t> saved(memory + board, memory + board + 50);
        unsigned hwin = get("HWIN"), cwin = get("CWIN"), draws = get("DRWS");
        call_guest(addresses.at("_KEYMA"), {letter});
        assert(reg.H() == 7);
        unsigned stack_used;
        unsigned steps = call_guest(addresses.at("_DOKEY"), {7},
                                   1000000, &stack_used);
        assert(stack_used < 1024 && get("CROW") * 5 + get("CCOL") == expected);
        assert(get("GSTAT") == 0 && get("TURN") == 1 && get("RESULT") == 0);
        assert(get("HWIN") == hwin && get("CWIN") == cwin && get("DRWS") == draws);
        assert(std::memcmp(memory + board, saved.data(), saved.size()) == 0);
        assert(output.find("Finding tip...") != std::string::npos);
        assert(output.find("Tip: highlighted square. SPACE to play.\033[0m") !=
               std::string::npos);
        printf("Z80 tip: %u instructions, %u stack bytes\n", steps, stack_used);
    };

    reset();
    hint('t', 12);
    output.clear();
    call_guest(addresses.at("_DOKEY"), {4});
    call_guest(addresses.at("TICTAC:console_flush"), {});
    assert(get("CCOL") == 3 && output.find("\033[26;18H\033[K") == 0);
    reset();
    call_guest(addresses.at("_DOKEY"), {4});
    call_guest(addresses.at("TICTAC:console_flush"), {});
    assert(output.find("\033[26;18H\033[K") == std::string::npos);

    reset();
    for (unsigned cell = 0; cell < 3; ++cell) {
        set(cell, 1);
        set(cell + 5, 2);
    }
    hint('T', 3);
    output.clear();
    call_guest(addresses.at("_DOKEY"), {5});
    call_guest(addresses.at("TICTAC:console_flush"), {});
    assert(memory[board + 6] == 1 && get("GSTAT") == 1 && get("RESULT") == 1);
    assert(output.find("\033[26;18H\033[K") == 0);

    reset();
    for (unsigned cell = 0; cell < 3; ++cell) set(cell, 2);
    set(5, 1);
    set(6, 1);
    set(24, 1);
    hint('t', 3);

    reset();
    for (unsigned row = 0; row < 5; ++row)
        for (unsigned column = 0; column < 5; ++column)
            set(row * 5 + column, fork[row][column] == 'X' ? 2 :
                                 fork[row][column] == 'O' ? 1 : 0);
    set(24, 0);
    hint('T', 12);
    output.clear();
    puts("Z80 T/t tips: human win/block/tactics, unchanged board/scores, highlight, SPACE and reset PASS");
}

static void minimax_tests(const std::map<std::string, unsigned> &addresses)
{
    const char *fork[] = {"..XOX", "XXO..", ".X.XO", "X.O.O", "OO..X"};
    unsigned board = addresses.at("TICTAC:_BRD");
    auto reset = [&]() { call_guest(addresses.at("_GINIT"), {}); };
    auto set = [&](unsigned cell, uint8_t piece) {
        memory[board + 2 * cell] = piece;
        memory[board + 2 * cell + 1] = 0;
    };
    auto search = [&](unsigned expected) {
        std::vector<uint8_t> saved(memory + board, memory + board + 50);
        call_guest(addresses.at("TICTAC:aiprep"), {2});
        unsigned stack_used;
        unsigned steps = call_guest(addresses.at("TICTAC:minimax"),
                                   {2, 2, uint16_t(-32000), 32000},
                                   1000000, &stack_used);
        assert(reg.H() == expected && stack_used < 1024);
        assert(std::memcmp(memory + board, saved.data(), saved.size()) == 0);
        printf("Z80 minimax score %u: %u instructions, %u stack bytes\n",
               expected, steps, stack_used);
    };
    auto move = [&]() {
        unsigned stack_used;
        unsigned steps = call_guest(addresses.at("_AIMOV"), {}, 1000000, &stack_used);
        assert(stack_used < 1024);
        call_guest(addresses.at("TICTAC:console_flush"), {});
        output.clear();
        printf("Z80 CPU move: %u instructions, %u stack bytes\n", steps, stack_used);
    };

    reset();
    for (unsigned row = 0; row < 5; ++row)
        for (unsigned column = 0; column < 5; ++column)
            set(row * 5 + column, fork[row][column] == 'X' ? 1 :
                                 fork[row][column] == 'O' ? 2 : 0);
    search(9);
    move();
    assert(memory[board + 24] == 2);

    reset();
    for (unsigned cell = 0; cell < 3; ++cell) {
        set(cell, 2);
        set(cell + 5, 1);
    }
    set(24, 1);
    search(30001);
    move();
    assert(memory[board + 6] == 2);

    reset();
    for (unsigned cell = 0; cell < 3; ++cell) set(cell, 1);
    set(5, 2);
    set(6, 2);
    move();
    assert(memory[board + 6] == 2);

    reset();
    set(12, 1);
    set(7, 1);
    set(18, 2);
    move();
    unsigned humans = 0, computers = 0;
    for (unsigned cell = 0; cell < 25; ++cell) {
        humans += memory[board + 2 * cell] == 1;
        computers += memory[board + 2 * cell] == 2;
    }
    assert(humans == 2 && computers == 2 && memory[board + 36] == 2);
    puts("Z80 two-ply minimax: tactical horizon, win priority, blocking, early-game cost and stack PASS");
}

int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 3);
    bool escape_only = argc == 3;
    std::string base = argv[1];
    std::ifstream binary(base + ".COM", std::ios::binary);
    assert(binary.good());
    std::vector<uint8_t> image(std::istreambuf_iterator<char>(binary), {});
    std::ifstream listing(base + ".SYM");
    assert(listing.good());
    std::string text(std::istreambuf_iterator<char>(listing), {});
    std::map<unsigned, std::string> symbols;
    std::map<std::string, unsigned> addresses;
    std::regex re("([0-9A-F]{4})[ \\t]+([A-Za-z_][A-Za-z_0-9:]*)");
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
        unsigned address = std::stoul((*it)[1], nullptr, 16);
        symbols[address] = (*it)[2];
        addresses[(*it)[2]] = address;
    }
    std::memset(memory, 0, 65536);
    std::memcpy(memory + 0x100, image.data(), image.size());
    memory[5] = 0xc3;
    memory[6] = 0;
    memory[7] = 0xf0;
    /* WBOOT vector identifies the BIOS jump table; intercept CONOUT below. */
    memory[1] = 0x03;
    memory[2] = 0xf1;
    z80_reset(&cpu, no_in, no_out, no_in, &disk, input, out);
    console_tests(addresses);
    reg.pc = 0x100;
    reg.sp = 0xeff0;
    for (unsigned steps = 0; steps < 20000000; ++steps) {
        if (reg.pc == 0) {
            assert(placed && quit && starts[2] > 0);
            assert(drawing_keys.size() == 2 && drawing_key_position == 2);
            if (!escape_only) assert(starts[1] == 2 && random_calls > 0 && arrow_position == 4);
            assert(output.find("Thanks for playing!") != std::string::npos);
            assert(output.find("\033[2J\033[H\033[?25l\033[1;1H\033[36mTICTAC") == 0);
            assert(output.find("\033[8;1H\033[36mYou(X): \033[33m0") != std::string::npos);
            assert(output.find("\033[28;1H\033[?25h\033[0mThanks for playing!\r\n") !=
                   std::string::npos);
            puts(escape_only ? "Z80 lone Escape PASS" :
                "Z80 two turns, arrows and timer/random protocol PASS");
            early_draw_tests(addresses);
            opening_tests(addresses);
            if (!escape_only) {
                minimax_tests(addresses);
                tip_tests(addresses);
            }
            return 0;
        }
        if (reg.pc == 0xf10c) {
            output += char(reg.c);
            if (output.size() == 100)
                drawing_keys = {19, 4};
            reg.pc = memory[reg.sp] + 256 * memory[reg.sp + 1];
            reg.sp += 2;
        } else if (reg.pc == 5) {
            unsigned byte = 0;
            if (reg.c == 6) {
                assert(reg.e == 255);
                if (drawing_key_position < drawing_keys.size())
                    byte = drawing_keys[drawing_key_position++];
                else if (!placed && output.find("Your turn") != std::string::npos) {
                    byte = escape_only ? 27 : ' ';
                    placed = true;
                    if (escape_only) quit = true;
                } else if (!escape_only && placed && starts[1] &&
                           output.rfind("Your turn") >
                           output.find("CPU thinking")) {
                    if (arrow_position < 4) {
                        const uint8_t arrow_move[] = {27, '[', 'A', ' '};
                        byte = arrow_move[arrow_position++];
                    } else if (starts[1] == 2 && output.rfind("Your turn") >
                               output.rfind("CPU thinking")) {
                        byte = 'q';
                        quit = true;
                    }
                }
            } else if (reg.c != 108) {
                fprintf(stderr, "Unexpected BDOS function %u\n", reg.c);
                return 1;
            }
            reg.a = byte;
            reg.h = 0;
            reg.l = byte;
            reg.pc = memory[reg.sp] + 256 * memory[reg.sp + 1];
            reg.sp += 2;
        } else x80_emulate(1);
    }
    auto symbol = symbols.upper_bound(reg.pc);
    if (symbol != symbols.begin()) --symbol;
    fprintf(stderr, "Guest stuck PC=%04x SP=%04x near %s (%04x), timer=%x duration=%u "
        "starts=%u/%u random=%u\n", reg.pc, reg.sp, symbol->second.c_str(), symbol->first,
        timer_command, duration, starts[1], starts[2], random_calls);
    return 1;
}
