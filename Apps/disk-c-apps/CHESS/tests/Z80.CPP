#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
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
static std::string output;
static std::deque<uint8_t> keys;
static unsigned timer_command, timer_bytes, duration, timer_polls;
static std::map<std::string, unsigned> functions;

static uint8_t input(uint8_t port)
{
    assert(port == 23 && timer_command == 0x20);
    return timer_polls++ < 3;
}

static void out(uint8_t port, uint8_t byte)
{
    assert(port == 23);
    if (timer_bytes) {
        if (timer_bytes == 2) duration = byte;
        else duration |= unsigned(byte) << 8;
        if (--timer_bytes == 0) {
            assert(duration == 120);
            timer_polls = 0;
        }
    } else {
        timer_command = byte;
        if (byte == 0x10) timer_bytes = 2;
        else assert(byte == 0x20);
    }
}

static void guest_return()
{
    reg.pc = memory[reg.sp] + 256 * memory[reg.sp + 1];
    reg.sp += 2;
}

static uint16_t call(const std::string &name, const std::vector<uint16_t> &args = {},
                     bool report = false)
{
    assert(functions.count(name));
    reg.sp = 0xe000;
    memory[reg.sp] = 0xf0;
    memory[reg.sp + 1] = 0xff;
    for (unsigned i = 0; i < args.size(); ++i) {
        memory[reg.sp + 2 + i * 2] = args[i] & 255;
        memory[reg.sp + 3 + i * 2] = args[i] >> 8;
    }
    reg.pc = functions.at(name);
    unsigned steps = 0, lowest_sp = reg.sp;
    for (; reg.pc != 0xfff0; ++steps) {
        assert(steps < 1000000000);
        if (reg.sp < lowest_sp) lowest_sp = reg.sp;
        if (reg.pc == 0xf10c) {
            output += char(reg.c);
            guest_return();
        } else if (reg.pc == 5) {
            assert(reg.c == 6 && reg.e == 255);
            unsigned byte = 0;
            if (!keys.empty()) {
                byte = keys.front();
                keys.pop_front();
            }
            reg.a = byte;
            reg.h = 0;
            reg.l = byte;
            guest_return();
        } else {
            assert(reg.pc != 0);
            x80_emulate(1);
        }
    }
    assert(0xe000 - lowest_sp < 4096);
    if (report)
        printf("Z80 %s: %u instructions, %u stack bytes\n",
               name.c_str(), steps, 0xe000 - lowest_sp);
    return reg.H();
}

static std::string read_file(const std::string &name)
{
    std::ifstream file(name, std::ios::binary);
    assert(file.good());
    return std::string(std::istreambuf_iterator<char>(file), {});
}

static void console_tests()
{
    call("cursor", {5, 18});
    call("color", {107});
    const std::string text = "$\r\n";
    std::memcpy(memory + 0xc000, text.c_str(), text.size() + 1);
    call("write_text", {0xc000});
    call("write_number", {0});
    call("write_byte", {' '});
    call("write_number", {32767});
    call("write_byte", {' '});
    call("write_number", {0x8000});
    assert(output.empty());
    call("console_flush");
    assert(output == "\033[5;18H\033[107m$\r\n0 32767 -32768");
    output.clear();
    std::string boundary(2047, '.');
    boundary += "\033[31m$-32768";
    std::memcpy(memory + 0xc000, boundary.c_str(), boundary.size() + 1);
    call("write_text", {0xc000});
    call("console_flush");
    assert(output == boundary);
    output.clear();
    for (unsigned i = 0; i < 256; ++i) call("write_byte", {uint16_t(i)});
    call("console_flush");
    call("console_flush");
    assert(output.size() == 256);
    for (unsigned i = 0; i < 256; ++i) assert(uint8_t(output[i]) == i);
    output.clear();
    puts("Z80 exact ANSI, integers, all bytes and buffer boundaries PASS");
}

static void search_tests()
{
    for (uint16_t level = 1; level <= 3; ++level) {
        call("new_position");
        call("position_key", {0xc000});
        assert(call("choose_move", {0xc100, uint16_t(level + 2)}, true) == 1);
        call("position_key", {0xc040});
        assert(std::memcmp(memory + 0xc000, memory + 0xc040, 35) == 0);
        assert(call("generate_moves", {0}) == 20);
        printf("Z80 level %u legal choice: %u-%u\n",
               level, memory[0xc100], memory[0xc101]);
    }
    puts("Z80 levels 1/2/3 (depths 3/4/5), bounded stack and search restoration PASS");
}

static void gameplay_tests()
{
    keys = {19, 4};
    call("start_game");
    assert(output.size() > 2048 && keys.size() == 2);
    assert(output.find("\033[2J\033[H\033[?25l") == 0);
    assert(output.find("Your turn (White)") != std::string::npos);
    std::regex cursors("\\x1b\\[([0-9]+);([0-9]+)H");
    unsigned cursor_count = 0;
    for (std::sregex_iterator it(output.begin(), output.end(), cursors), end; it != end; ++it) {
        unsigned row = std::stoul((*it)[1]), col = std::stoul((*it)[2]);
        assert(row >= 1 && row <= 30 && col >= 1 && col <= 80);
        ++cursor_count;
    }
    assert(cursor_count > 150);
    assert(call("read_action") == 258 && call("read_action") == 259);
    call("handle_action", {' '});
    call("handle_action", {256});
    call("handle_action", {256});
    call("handle_action", {13});
    if (output.find("e2-e4") == std::string::npos)
        fprintf(stderr, "Missing move display, screen tail: %s\n",
                output.substr(output.size() > 800 ? output.size() - 800 : 0).c_str());
    assert(output.find("e2-e4") != std::string::npos);
    assert(call("generate_moves", {0}) == 20);
    assert(call("choose_move", {0xc100, 2}) == 1);
    output.clear();
    call("play_move", {0xc100});
    assert(output.find("Your turn (White)") != std::string::npos);
    call("position_key", {0xc000});
    output.clear();
    call("show_tip");
    call("position_key", {0xc040});
    assert(std::memcmp(memory + 0xc000, memory + 0xc040, 35) == 0);
    assert(output.find("TIP: move to the * square") != std::string::npos);
    call("handle_action", {' '});
    keys = {27, '[', 'A'};
    assert(call("read_action") == 0 && call("read_action") == 0);
    assert(call("read_action") == 256);
    keys = {27};
    assert(call("read_action") == 0);
    for (unsigned i = 0; i < 3; ++i) assert(call("read_action") == 0);
    assert(call("read_action") == 260);
    output.clear();
    call("handle_action", {260});
    assert(output.find("Selection cancelled.") != std::string::npos);
    call("handle_action", {'N'});
    assert(call("generate_moves", {0}) == 20);
    output.clear();
    keys = {'q'};
    assert(call("main") == 0);
    assert(output.find("\033[0m\033[?25hThanks for playing CHESS!\r\n") !=
           std::string::npos);
    puts("Z80 drawing-time keys, human/CPU, tip, arrows, Escape, replay and Q PASS");
}

static void promotion_tests()
{
    /* Reach promotion through legal moves, without injecting board state. */
    const uint8_t opening[][4] = {
        {16, 48, 0, 4}, {97, 65, 0, 4}, {48, 65, 0, 0}, {103, 87, 0, 0},
        {65, 81, 0, 0}, {87, 71, 0, 0}, {81, 97, 0, 0}, {71, 55, 0, 0}
    };
    for (uint16_t piece = 2; piece <= 5; ++piece) {
        call("start_game");
        for (const auto &move : opening) {
            std::memcpy(memory + 0xc100, move, 4);
            call("play_move", {0xc100});
            output.clear();
        }
        for (unsigned i = 0; i < 5; ++i) call("handle_action", {256});
        for (unsigned i = 0; i < 3; ++i) call("handle_action", {258});
        call("handle_action", {' '});
        call("handle_action", {256});
        call("handle_action", {258});
        call("handle_action", {' '});
        assert(output.find("PROMOTE: 1 Queen") != std::string::npos);
        call("handle_action", {uint16_t('6' - piece)});
        assert(output.find(std::string("b7-a8") + "NBRQ"[piece - 2]) != std::string::npos);
        call("position_key", {0xc000});
        assert((memory[0xc000 + 28] & 15) == piece);
        assert((memory[0xc000 + 24] >> 4) == 0);
        assert(memory[0xc000 + 32] == 0);
        output.clear();
    }
    puts("Z80 all four keyboard promotion choices PASS");
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    std::string base = argv[1], image = read_file(base + ".COM");
    std::string listing = read_file(base + ".SYM");
    std::map<std::string, unsigned> symbols;
    std::regex sym("([0-9A-F]{4})[ \\t]+([A-Za-z_][A-Za-z_0-9:]*)");
    for (std::sregex_iterator it(listing.begin(), listing.end(), sym), end; it != end; ++it)
        symbols[(*it)[2]] = std::stoul((*it)[1], nullptr, 16);
    for (const auto &symbol : symbols)
        if (symbol.first.compare(0, 6, "CHESS:") == 0)
            functions[symbol.first.substr(6)] = symbol.second;
    functions["main"] = symbols.at("_MAIN");
    std::memset(memory, 0, 65536);
    std::memcpy(memory + 0x100, image.data(), image.size());
    memory[5] = 0xc3;
    memory[6] = 0;
    memory[7] = 0xf0;
    memory[1] = 3;
    memory[2] = 0xf1;
    z80_reset(&cpu, no_in, no_out, no_in, &disk, input, out);
    console_tests();
    search_tests();
    promotion_tests();
    gameplay_tests();
    return 0;
}
