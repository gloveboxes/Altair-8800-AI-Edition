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
    const uint8_t response[] = {2, 0, 0xff, 0xff};
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

static void call_guest(unsigned address, const std::vector<uint16_t> &arguments)
{
    reg.sp = 0xe000;
    memory[reg.sp] = 0xf0;
    memory[reg.sp + 1] = 0xff;
    for (unsigned index = 0; index < arguments.size(); ++index) {
        memory[reg.sp + 2 + 2 * index] = arguments[index] & 255;
        memory[reg.sp + 3 + 2 * index] = arguments[index] >> 8;
    }
    reg.pc = address;
    for (unsigned steps = 0; reg.pc != 0xfff0; ++steps) {
        assert(steps < 1000000);
        if (reg.pc == 0xf10c) {
            output += char(reg.c);
            reg.pc = memory[reg.sp] + 256 * memory[reg.sp + 1];
            reg.sp += 2;
        } else {
            assert(reg.pc != 0 && reg.pc != 5);
            x80_emulate(1);
        }
    }
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
            unsigned board = addresses.at("TICTAC:_BRD");
            for (unsigned row = 0; row < 5; ++row) {
                for (unsigned column = 0; column < 5; ++column) {
                    unsigned offset = board + 2 * (row * 5 + column);
                    memory[offset] = (row == 2 || column == 2 || row == column ||
                                      row + column == 4) ? 2 : 0;
                    memory[offset + 1] = 0;
                }
            }
            memory[board + 24] = 0;
            reg.sp = 0xe000;
            const uint8_t args[] = {0xf0, 0xff, 2, 0, 2, 0};
            std::memcpy(memory + reg.sp, args, sizeof(args));
            reg.pc = addresses.at("_AISCO");
            unsigned score_steps = 0;
            while (reg.pc != 0xfff0) {
                assert(++score_steps < 1000000);
                x80_emulate(1);
            }
            assert((uint32_t(reg.D()) << 16 | reg.H()) == 80020);
            puts(escape_only ? "Z80 lone Escape PASS" :
                "Z80 two turns, arrows, timer/random protocol and 32-bit AI score PASS");
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
