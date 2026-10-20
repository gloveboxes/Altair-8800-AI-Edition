// Executed by asm_basic_regression.mjs against real assembled COM files.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <string>
#include <vector>
extern "C" {
#include "memory.h"
#include "z80.h"
}

static int status, group, length, resets, acks, polls, mode;
static size_t frame_pos;
static std::vector<uint8_t> request, frame;
static std::string output;
static bool dynamic_reply;
static unsigned timer_queries;
static uint8_t timer_command;
static bool timer_parameter;
static std::vector<uint8_t> timer_writes;
static std::vector<std::vector<uint8_t>> transactions;
static uint8_t no_in() { return 0; }
static void no_out(uint8_t) {}
static disk_controller_t disk = {no_out, no_in, no_out, no_in, no_out, no_in};
static std::map<std::string, unsigned> symbols;
static z80_t cpu;

static uint8_t input(uint8_t port)
{
    if (port == 23) {
        if (timer_command == 35)
            return mode == 1 ? 255 : static_cast<uint8_t>(++timer_queries == 1);
        assert(timer_command == 80 || timer_command == 96);
        assert(frame_pos + 2 < frame.size());
        return frame[2 + frame_pos++];
    }
    if (port == 2) {
        if (status == 4 && mode != 2 && ++polls == 5)
            status = mode == 1 ? 255 : frame.empty() ? 1 : 2;
        return status;
    }
    assert(port == 4 && (status == 2 || status == 255));
    assert(frame_pos < frame.size());
    uint8_t byte = frame[frame_pos++];
    if (frame_pos == frame.size() && status == 2) status = 0;
    return byte;
}

static void out(uint8_t port, uint8_t byte)
{
    if (port == 23) {
        timer_writes.push_back(byte);
        if (timer_parameter) {
            transactions.push_back({timer_command, byte});
            timer_parameter = false;
            timer_queries = 0;
        } else {
            timer_command = byte;
            timer_parameter = byte == 19;
        }
        return;
    }
    if (port == 2) {
        if (byte == 1) { ++resets; status = 0; return; }
        if (byte == 0) { assert(status == 1); ++acks; status = 0; return; }
        assert(status == 0);
        group = byte;
        length = -2;
        request.clear();
        status = 3;
        return;
    }
    assert(port == 3 && status == 3);
    if (length == -2) { length = -1; request.push_back(byte); return; }
    if (length == -1) {
        length = request[0] + 256 * byte;
        request.clear();
        if (!length) status = 4;
        return;
    }
    request.push_back(byte);
    assert(request.size() <= static_cast<size_t>(length));
    if (request.size() != static_cast<size_t>(length)) return;
    status = 4;
    if (!dynamic_reply) return;
    transactions.push_back(request);
    frame.clear();
    frame_pos = 0;
    polls = 0;
    if (group == 16) {
        if (request[0] == 0) timer_queries = 0;
        else frame = {1, 0, static_cast<uint8_t>(++timer_queries == 1)};
    } else if (group == 32) {
        std::string value = request[0] == 4 ? "Tue 10/20/2026 17:30:00" : "12345";
        frame = {static_cast<uint8_t>(value.size()), 0};
        frame.insert(frame.end(), value.begin(), value.end());
    } else if (group == 33) frame = {4, 0, 't', 'e', 's', 't'};
    else assert(false);
}

static void load(const std::string& app)
{
    symbols.clear();
    std::ifstream listing(app + (app == "ATTNZ80" ? ".PRN" : ".SYM"));
    std::string text((std::istreambuf_iterator<char>(listing)), {});
    std::regex re("([0-9A-F]{4})[ \\t]+([A-Z][A-Z0-9]*)");
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it)
        symbols[(*it)[2].str()] = std::stoul((*it)[1].str(), nullptr, 16);
    memset(memory, 0, 65536);
    std::ifstream binary(app + ".COM", std::ios::binary);
    binary.read(reinterpret_cast<char*>(memory + 0x100), 0xee00);
    assert(binary.gcount() > 200);
    z80_reset(&cpu, no_in, no_out, no_in, &disk, input, out);
}

static void setup(int m, const std::vector<uint8_t>& response)
{
    mode = m;
    status = group = resets = acks = polls = 0;
    dynamic_reply = false;
    transactions.clear();
    timer_writes.clear();
    timer_command = timer_queries = 0;
    timer_parameter = false;
    frame_pos = 0;
    request.clear();
    output.clear();
    frame.clear();
    if (!response.empty() || m == 1 || m == 3) {
        frame.push_back(response.size() & 255);
        frame.push_back(response.size() >> 8);
        frame.insert(frame.end(), response.begin(), response.end());
    }
    cpu.registers.sp = 0xeffe;
    memory[0xeffe] = 0xf0;
    memory[0xefff] = 0xff;
}

static bool run(const std::string& entry)
{
    cpu.registers.pc = symbols.at(entry);
    unsigned steps = 0;
    while (cpu.registers.pc != 0xfff0 && cpu.registers.pc != 0) {
        assert(++steps < 30000000);
        if (cpu.registers.pc != 5) {
            z80_cycle(&cpu);
            continue;
        }
        if (cpu.registers.c == 9) {
            unsigned p = 256 * cpu.registers.d + cpu.registers.e;
            while (memory[p] != '$') output += memory[p++];
        } else if (cpu.registers.c == 2) output += char(cpu.registers.e);
        else if (cpu.registers.c == 6) cpu.registers.a = 0;
        else assert(false);
        unsigned sp = cpu.registers.sp;
        cpu.registers.pc = memory[sp] + 256 * memory[sp + 1];
        cpu.registers.sp += 2;
    }
    return cpu.registers.pc == 0xfff0;
}

static void args()
{
    cpu.registers.a = 17;
    cpu.registers.b = 0;
    cpu.registers.c = 4;
    cpu.registers.d = 0x45;
    cpu.registers.e = 0x67;
    cpu.registers.h = 0x90;
    cpu.registers.l = 0;
    memory[0x9000] = 1;
    memory[0x9001] = 0;
    memory[0x9002] = 255;
    memory[0x9003] = 0;
}

static unsigned word(const std::string& name)
{
    unsigned p = symbols.at(name);
    return memory[p] + 256 * memory[p + 1];
}

int main()
{
    for (const char* app : {"DATE", "ENV", "ESP32"}) {
        load(app);
        assert(symbols.count("SVREQ") && symbols.count("SVBUF"));
        setup(0, {0x34, 0x12, 0, 0x80});
        args();
        assert(run("SVREQ"));
        assert(group == 17 && request == std::vector<uint8_t>({1, 0, 255, 0}));
        assert(status == 0 && frame_pos == frame.size() && !resets);
        assert(word("SVLEN") == 4 && memory[symbols["SVBUF"] + 3] == 0x80);
        assert(cpu.registers.b == 0 && cpu.registers.c == 4);
        assert(cpu.registers.d == 0x45 && cpu.registers.e == 0x67);
        assert(cpu.registers.h == 0x90 && cpu.registers.l == 0);
        assert(cpu.registers.sp == 0xf000);
        setup(0, {}); args(); assert(run("SVREQ"));
        assert(acks == 1 && status == 0 && word("SVLEN") == 0);
        setup(3, {}); args(); assert(run("SVREQ"));
        assert(frame_pos == 2 && word("SVLEN") == 0 && acks == 0);
        setup(0, std::vector<uint8_t>(2048, 0x55)); args();
        assert(run("SVREQ") && frame_pos == 2050 && word("SVLEN") == 2048);
        setup(1, {0x15, 'n', 'o'}); args(); assert(!run("SVREQ"));
        assert(frame_pos == frame.size() && resets == 1 && status == 0);
        assert(output.find("Service error/timeout") != std::string::npos);
        setup(2, {}); args(); assert(!run("SVREQ"));
        assert(resets == 1 && status == 0);
        setup(0, std::vector<uint8_t>(2049, 1)); args(); assert(!run("SVREQ"));
        assert(resets == 1 && status == 0 && frame_pos == 2);
        setup(1, {0x14}); args(); status = 255; assert(!run("SVREQ"));
        assert(frame_pos == frame.size() && resets == 1);
        printf("%s: asynchronous framing, ACK, zero/max length, error drain, timeout, capacity, registers/stack PASS\n", app);
    }
    for (const char* app : {"ATTNINF", "ATTNZ80"}) {
        load(app);
        for (unsigned operation = 0; operation < 3; ++operation) {
            setup(0, {0x34, 0x12, 0x56, 0x78});
            args();
            cpu.registers.a = operation;
            assert(run("SWATCH"));
            assert(timer_writes == std::vector<uint8_t>({uint8_t(64 + 16 * operation)}));
            assert(frame_pos == (operation ? 4U : 0U));
            assert(!group && !acks && !resets && request.empty());
            if (operation) assert(word("SVBUF") == 0x1234);
            assert(cpu.registers.b == 0 && cpu.registers.c == 4);
            assert(cpu.registers.d == 0x45 && cpu.registers.e == 0x67);
            assert(cpu.registers.h == 0x90 && cpu.registers.l == 0);
            assert(cpu.registers.sp == 0xf000);
        }
        printf("%s: dedicated stopwatch commands, LE bytes and preserved registers PASS\n", app);
    }
    load("ENV");
    setup(0, {});
    strcpy(reinterpret_cast<char*>(memory + 0x9000), "Key");
    strcpy(reinterpret_cast<char*>(memory + 0x9020), "Mixed value");
    cpu.registers.h = 0x90; cpu.registers.l = 0;
    cpu.registers.d = 0x90; cpu.registers.e = 0x20;
    assert(run("ESET"));
    assert(group == 48 && request == std::vector<uint8_t>({
        3, 3, 'K', 'e', 'y', 11, 0, 'M', 'i', 'x', 'e', 'd', ' ', 'v', 'a', 'l', 'u', 'e'}));
    setup(0, {});
    cpu.registers.h = 0x90; cpu.registers.l = 0;
    assert(run("EDELK"));
    assert(group == 48 && request == std::vector<uint8_t>({4, 3, 'K', 'e', 'y'}));
    setup(0, {'A', '=', '1', 13, 10, 'B', '=', '2', 13, 10});
    assert(run("LOADAL") && memory[symbols["NVAR"]] == 2);
    assert(group == 48 && request == std::vector<uint8_t>({5}));
    assert(std::string(reinterpret_cast<char*>(memory + symbols["KEYS"])) == "A");
    assert(std::string(reinterpret_cast<char*>(memory + symbols["VALS"])) == "1");
    puts("ENV: SET lengths, DELETE, LIST parsing PASS");
    load("SLEEP"); setup(0, {}); dynamic_reply = true;
    memory[0x80] = 4; memcpy(memory + 0x81, " 256", 4);
    assert(run("START"));
    assert(!resets && acks == 0 && status == 0);
    assert(transactions == std::vector<std::vector<uint8_t>>({{19, 255}, {19, 1}}));
    assert(timer_writes == std::vector<uint8_t>({19, 255, 35, 19, 1, 35}));
    puts("SLEEP: 255+1-second chunks, running/expired queries, BDOS key polling PASS");
    setup(1, {});
    memory[0x80] = 2; memcpy(memory + 0x81, " 1", 2);
    assert(!run("START"));
    assert(output.find("Timer error/timeout; interface reset") != std::string::npos);
    load("DATE"); setup(0, {}); dynamic_reply = true;
    assert(run("START"));
    assert(output == "Tue 10/20/2026 17:30:00\r\n");
    assert(group == 32 && transactions == std::vector<std::vector<uint8_t>>({{4}}));
    puts("DATE: full counted DATE-format output PASS");
    load("ESP32"); setup(0, {}); dynamic_reply = true;
    assert(run("START"));
    assert(!resets && status == 0 && frame_pos == frame.size());
    assert(transactions == std::vector<std::vector<uint8_t>>({{1, 0}, {1, 1}, {1, 2}, {3}, {0}}));
    assert(output.find("12345") != std::string::npos);
    puts("ESP32: info/version/uptime formatting PASS");
}
