#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <cctype>
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
#include "x80.hxx"

static z80_t cpu;
static int status, completion, group, resets, acks, begin_status;
static unsigned busy, reads, length;
static size_t frame_pos;
static std::vector<uint8_t> request, frame, image;
static std::vector<uint8_t> timer_writes;
static size_t timer_reads;
static std::string output;
static std::map<std::string, unsigned> symbols;
static uint8_t no_in() { return 0; }
static void no_out(uint8_t) {}
static disk_controller_t disk = {no_out, no_in, no_out, no_in, no_out, no_in};

static std::string symbol_key(const std::string& name)
{
    std::string key = "_" + name;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char character) { return std::toupper(character); });
    return key.substr(0, 6);
}

static unsigned symbol_address(const std::string& name)
{
    return symbols.at(symbol_key(name));
}

static uint8_t input(uint8_t port)
{
    if (port == 23) {
        assert(timer_reads + 2 < frame.size());
        return frame[2 + timer_reads++];
    }
    if (port == 2) {
        ++reads;
        if (status == 4) {
            if (busy) {
                --busy;
                return 4;
            }
            status = completion;
        }
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
        return;
    }
    if (port == 2) {
        if (byte == 1) { ++resets; status = 0; return; }
        if (byte == 0) { assert(status == 1); ++acks; status = 0; return; }
        assert(status == 0);
        group = byte;
        status = begin_status;
        return;
    }
    assert(port == 3 && status == 3);
    request.push_back(byte);
    if (request.size() == 2) length = request[0] + 256 * request[1];
    if (request.size() >= 2 && request.size() == length + 2) status = 4;
}

static void load(const std::string& base, bool all_helpers = true)
{
    std::ifstream binary(base + ".COM", std::ios::binary);
    assert(binary.good());
    image.assign(std::istreambuf_iterator<char>(binary), {});
    std::ifstream listing(base + ".SYM");
    assert(listing.good());
    std::string text((std::istreambuf_iterator<char>(listing)), {});
    std::regex re("([0-9A-F]{4})[ \\t]+([A-Za-z_][A-Za-z_0-9:]*)");
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
        std::string name = (*it)[2];
        unsigned address = std::stoul((*it)[1], nullptr, 16);
        assert(symbols.emplace(name, address).second);
        size_t separator = name.rfind(':');
        if (separator != std::string::npos &&
            name.substr(separator + 1) == "TIMER_INVALID_ARGUMENT")
            symbols.emplace("TIMER_INVALID_ARGUMENT", address);
    }
    std::map<std::string, std::string> exported_keys;
    for (const std::string& name : {
             "svc_begin_request", "svc_finish_response", "svc_call_service",
             "svc_operation_send", "svc_get_text", "svc_query_text",
             "svc_start_timer", "svc_timer_active", "svc_access_stopwatch",
             "svc_random_u16", "io_wait_completion", "io_write_request",
             "io_read_response", "io_drain_response", "io_abort_timer"}) {
        assert(exported_keys.emplace(symbol_key(name), name).second);
        if (all_helpers)
            assert(symbol_address(name));
    }
}

static void setup(int final_status, const std::vector<uint8_t>& payload, unsigned polls = 0)
{
    std::memset(memory, 0, 65536);
    assert(image.size() < 0x8000);
    std::memcpy(memory + 0x100, image.data(), image.size());
    z80_reset(&cpu, no_in, no_out, no_in, &disk, input, out);
    status = group = resets = acks = reads = length = 0;
    completion = final_status;
    begin_status = 3;
    busy = polls;
    frame_pos = 0;
    request.clear();
    timer_writes.clear();
    timer_reads = 0;
    output.clear();
    frame = {uint8_t(payload.size()), uint8_t(payload.size() >> 8)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    for (unsigned i = 0; i < 2048; ++i) memory[0x9000 + i] = uint8_t(i);
    std::memset(memory + 0xa000, 0xa5, 2050);
}

static uint64_t call(const std::string& name, const std::vector<unsigned>& args,
                     int expected, bool exits = false, bool interrupts = true)
{
    constexpr unsigned stack = 0xe000;
    memory[stack] = 0xf0;
    memory[stack + 1] = 0xff;
    for (size_t i = 0; i < args.size(); ++i) {
        memory[stack + 2 + i * 2] = uint8_t(args[i]);
        memory[stack + 3 + i * 2] = uint8_t(args[i] >> 8);
    }
    reg.sp = stack;
    reg.pc = symbol_address(name);
    reg.ix = 0x1234;
    reg.iy = 0x5678;
    reg.fINTE = reg.fINTE2 = interrupts;
    uint64_t cycles = 0;
    unsigned steps = 0;
    bool exited = false;
    while (reg.pc != 0xfff0) {
        assert(++steps < 500000000);
        if (exits && reg.pc == symbols.at("_EXIT")) {
            assert(memory[reg.sp + 2] == 1 && memory[reg.sp + 3] == 0);
            exited = true;
            break;
        }
        if (exits && symbols.count("TIMER_INVALID_ARGUMENT") &&
            reg.pc == symbol_address("io_abort_timer")) {
            assert(reg.sp == stack - 4);
            assert(memory[reg.sp + 2] == expected && memory[reg.sp + 3] == 0);
            assert(reg.ix == 0x1234 && reg.iy == 0x5678);
        }
        if (reg.pc == 5) {
            if (reg.c == 9) {
                unsigned p = reg.D();
                while (memory[p] != '$') output += char(memory[p++]);
            } else if (reg.c == 2) output += char(reg.e);
            else assert(false);
            reg.pc = memory[reg.sp] + 256 * memory[reg.sp + 1];
            reg.sp += 2;
        } else cycles += x80_emulate(1);
    }
    assert(exited == exits);
    assert(reg.fINTE == interrupts && reg.fINTE2 == interrupts);
    if (!exits) {
        assert(reg.sp == stack + 2 && reg.ix == 0x1234 && reg.iy == 0x5678);
        for (size_t i = 0; i < args.size(); ++i)
            assert(memory[stack + 2 + i * 2] + 256 * memory[stack + 3 + i * 2] == args[i]);
    }
    if (!exits && expected != -65536)
        assert(reg.H() == uint16_t(expected));
    return cycles;
}

static void invalid_timer(const std::string& name, const std::vector<unsigned>& args, int code)
{
    setup(1, {});
    status = 4;
    call(name, args, code, true);
    assert(timer_writes.empty() && timer_reads == 0);
    assert(reads == 0 && request.empty() && status == 4 && resets == 0);
    const std::string expected = "\r\r\nService I/O error " + std::to_string(code) + "\r\r\n";
    assert(output == expected);
}

int main(int argc, char** argv)
{
    assert(argc == 2 || (argc == 3 && std::string(argv[2]) == "--date"));
    load(argv[1], argc == 2);
    if (argc == 3) {
        assert(image.size() <= 2048);
        const std::string date = "Tue 10/20/2026 17:30:00";
        setup(2, std::vector<uint8_t>(date.begin(), date.end()), 5);
        call("main", {}, 0);
        assert(output == date + "\r\r\n");
        assert(group == 32 && request == std::vector<uint8_t>({1, 0, 4}));
        assert(status == 0 && frame_pos == frame.size() && resets == 0);
        setup(255, {21, 'n', 'o'});
        call("main", {}, 1);
        assert(output == "\r\r\nService I/O error 21\r\r\n");
        assert(resets == 1 && frame_pos == frame.size());
        setup(2, std::vector<uint8_t>(80, 'x'));
        call("main", {}, 1);
        assert(output == "\r\r\nService I/O error 19\r\r\n");
        assert(resets == 1 && frame_pos == frame.size());
        puts("DATE C: size limit, exact output, asynchronous reply, error/oversize drain and nonzero exit PASS");
        return 0;
    }
    const std::vector<unsigned> raw = {48, 0x9000, 2, 0xa000, 8};
    const std::vector<uint8_t> bytes = {0, 128, 255, 0};
    for (unsigned count : {1U, 255U, 256U, 2047U, 2048U}) {
        setup(1, {});
        call("svc_begin_request", {48, count}, 0);
        assert(group == 48 && status == 3 && resets == 0);
        assert(request == std::vector<uint8_t>({uint8_t(count), uint8_t(count >> 8)}));
    }
    for (unsigned count : {0U, 2049U, 32767U, 32768U, 65535U}) {
        setup(1, {});
        call("svc_begin_request", {48, count}, -3);
        assert(reads == 0 && request.empty() && resets == 1);
    }
    for (int state : {1, 2, 3, 4, 255}) {
        setup(state, {});
        status = state;
        call("svc_begin_request", {48, 1}, -8);
        assert(request.empty() && resets == 1);
    }
    for (int state : {0, 1, 2, 4, 255}) {
        setup(state, {});
        begin_status = state;
        call("svc_begin_request", {48, 1}, -1);
        assert(reads == 2 && request.empty() && resets == 1);
    }
    for (unsigned capacity : {2049U, 32767U, 32768U, 65535U}) {
        setup(2, bytes);
        status = 4;
        call("svc_finish_response", {0xa000, capacity}, -3);
        assert(reads == 0 && frame_pos == 0 && resets == 1);
    }
    setup(2, bytes);
    status = 4;
    call("svc_finish_response", {0, 1}, -3);
    assert(reads == 0 && frame_pos == 0 && resets == 1);
    for (unsigned count : {0U, 1U, 255U, 256U, 2048U, 2049U}) {
        const std::vector<uint8_t> payload(count, 0x7e);
        for (unsigned capacity : {0U, 1U, 255U, 256U, 2048U}) {
            setup(2, payload);
            status = 4;
            const bool invalid = count > 2048;
            const bool excess = count > capacity;
            call("svc_finish_response", {capacity ? 0xa000U : 0U, capacity},
                 invalid ? -3 : excess ? -19 : int(count));
            assert(frame_pos == (invalid ? 2 : count + 2));
            assert(resets == int(invalid || excess));
            const unsigned copied = invalid ? 0 : std::min(count, capacity);
            for (unsigned index = 0; index < copied; ++index)
                assert(memory[0xa000 + index] == 0x7e);
            assert(memory[0xa000 + copied] == 0xa5);
        }
    }
    for (int state : {0, 3, 5, 254}) {
        setup(state, {});
        status = state;
        call("svc_finish_response", {0, 0}, -8);
        assert(frame_pos == 0 && resets == 1);
    }
    for (unsigned code = 0; code <= 255; ++code) {
        setup(255, {uint8_t(code), 0, 255});
        call("svc_call_service", raw, -int(code), false, code % 2 != 0);
        assert(output == "\r\r\nService I/O error " + std::to_string(code) + "\r\r\n");
        assert(frame_pos == 5 && resets == 1 && memory[0xa000] == 0xa5);
    }
    setup(2, bytes);
    call("svc_call_service", raw, 4);
    assert(group == 48 && request == std::vector<uint8_t>({2, 0, 0, 1}));
    assert(std::memcmp(memory + 0xa000, bytes.data(), 4) == 0 && status == 0);
    assert(memory[0xa004] == 0xa5);
    setup(1, {});
    call("svc_operation_send", {80, 1, 0, 0, 0, 0}, 0);
    assert(request == std::vector<uint8_t>({1, 0, 1}) && acks == 1);
    setup(2, {});
    call("svc_call_service", raw, 0);
    assert(frame_pos == 2 && status == 0 && memory[0xa000] == 0xa5);
    setup(2, bytes);
    call("svc_call_service", {48, 0x9000, 2, 0xa000, 2}, -19);
    assert(frame_pos == 6 && resets == 1 && memory[0xa002] == 0xa5);
    assert(output.find("Service I/O error 19") != std::string::npos);
    setup(2, bytes);
    call("svc_call_service", {48, 0x9000, 2, 0, 0}, -19);
    assert(frame_pos == 6 && resets == 1);
    setup(255, {255, 128, 0});
    call("svc_call_service", raw, -255);
    assert(frame_pos == 5 && resets == 1 && memory[0xa000] == 0xa5);
    assert(output.find("Service I/O error 255") != std::string::npos);
    // Reuse the live channel after RESET, without restoring guest memory.
    completion = 1;
    request.clear();
    call("svc_operation_send", {80, 1, 0, 0, 0, 0}, 0);
    assert(acks == 1 && status == 0);
    setup(255, {});
    call("svc_call_service", raw, -8);
    assert(frame_pos == 2 && resets == 1);
    setup(2, std::vector<uint8_t>(2049, 7));
    call("svc_call_service", raw, -3);
    assert(frame_pos == 2 && resets == 1);
    setup(1, {});
    call("svc_call_service", {48, 0x9000, 0, 0xa000, 8}, -3);
    assert(request.empty() && resets == 1);
    setup(1, {});
    call("svc_operation_send", {80, 1, 0, 1, 0xa000, 8}, -3);
    assert(request.empty() && resets == 1);
    setup(1, {});
    call("svc_call_service", {48, 0x9000, 2, 0, 1}, -3);
    assert(request.empty() && resets == 1);
    setup(1, {});
    call("svc_operation_send", {80, 1, 0x9000, 2048, 0, 0}, -3);
    assert(request.empty() && resets == 1);

    setup(1, {}, 60000);
    uint64_t polling = call("svc_call_service", raw, 0);
    assert(reads == 60003 && acks == 1);
    for (unsigned wait : {65534U, 65535U, 6553499U, 6553500U}) {
        setup(1, {}, wait);
        bool timeout = wait == 6553500;
        call("svc_call_service", raw, timeout ? -7 : 0);
        assert(reads == 2 + (timeout ? wait : wait + 1));
        assert(timeout ? resets == 1 : acks == 1);
    }

    std::vector<uint8_t> large(2048);
    for (unsigned i = 0; i < large.size(); ++i) large[i] = uint8_t(i);
    setup(2, large);
    uint64_t transfer = call("svc_operation_send", {80, 1, 0x9000, 2047, 0xa000, 2048}, 2048);
    assert(group == 80 && request.size() == 2050 && request[2] == 1);
    assert(std::memcmp(request.data() + 3, memory + 0x9000, 2047) == 0);
    assert(std::memcmp(memory + 0xa000, large.data(), 2048) == 0);
    assert(memory[0xa800] == 0xa5 && status == 0 && frame_pos == 2050);
    setup(1, {});
    call("svc_call_service", {48, 0x9000, 2048, 0, 0}, 0);
    assert(request.size() == 2050 && acks == 1);
    setup(2, large);
    call("svc_call_service", {48, 0x9000, 2, 0xa000, 1}, -19);
    assert(frame_pos == 2050 && resets == 1 && memory[0xa001] == 0xa5);
    setup(2, {'a', 'b', 'c'});
    call("svc_get_text", {32, 0, 0xa000, 8}, 3);
    assert(std::memcmp(memory + 0xa000, "abc\0", 4) == 0);
    setup(2, {'x'});
    call("svc_query_text", {64, 0, 14, 0xa000, 8}, 1);
    assert(request == std::vector<uint8_t>({2, 0, 0, 14}));
    assert(memory[0xa000] == 'x' && memory[0xa001] == 0);
    setup(1, {});
    uint64_t start_seconds = call("svc_start_timer", {3, 255}, 0);
    assert(timer_writes == std::vector<uint8_t>({0x13, 255}));
    assert(request.empty() && reads == 0);
    uint64_t start_ms = 0;
    for (unsigned id = 0; id < 4; ++id) {
        for (unsigned delay : {0U, 1U, 255U, 256U, 65535U}) {
            if (id == 3 && delay > 255) continue;
            setup(1, {});
            uint64_t cycles = call("svc_start_timer", {id, delay}, 0);
            std::vector<uint8_t> expected = {uint8_t(0x10 + id), uint8_t(delay)};
            if (id != 3) expected.push_back(uint8_t(delay >> 8));
            assert(timer_writes == expected);
            assert(request.empty() && reads == 0 && timer_reads == 0);
            if (id == 0 && delay == 65535) start_ms = cycles;
        }
    }
    uint64_t query_cycles[2] = {};
    for (unsigned id = 0; id < 4; ++id) {
        for (unsigned running : {0U, 1U}) {
            setup(2, {uint8_t(running)});
            status = 4;
            query_cycles[running] = call("svc_timer_active", {id}, running);
            assert(timer_writes == std::vector<uint8_t>({uint8_t(0x20 + id)}));
            assert(timer_reads == 1 && reads == 0 && status == 4 && request.empty());
        }
    }
    setup(2, {0, 128});
    call("svc_random_u16", {}, 32768);
    uint64_t watch_cycles[3] = {};
    for (unsigned id = 0; id < 3; ++id) {
        for (unsigned operation = 0; operation < 3; ++operation) {
            setup(2, bytes);
            status = 4;
            watch_cycles[operation] = call("svc_access_stopwatch", {id, operation, operation ? 0xa000U : 0U}, operation ? 4 : 0);
            assert(timer_writes == std::vector<uint8_t>({uint8_t(0x40 + 16 * operation + id)}));
            assert(timer_reads == (operation ? 4U : 0U));
            assert(reads == 0 && request.empty() && status == 4);
            if (operation)
                assert(std::memcmp(memory + 0xa000, bytes.data(), 4) == 0 && memory[0xa004] == 0xa5);
        }
        for (unsigned bad : {4U, 255U, 256U, 257U, 32767U, 32768U, 65535U}) {
            invalid_timer("svc_start_timer", {bad, 1}, 4);
            invalid_timer("svc_timer_active", {bad}, 4);
            invalid_timer("svc_access_stopwatch", {bad, 0, 0}, 4);
        }
        invalid_timer("svc_access_stopwatch", {3, 0, 0}, 4);
        for (unsigned bad : {3U, 255U, 256U, 257U, 32768U, 65535U})
            invalid_timer("svc_access_stopwatch", {0, bad, 0xa000}, 4);
        for (unsigned delay : {256U, 65535U})
            invalid_timer("svc_start_timer", {3, delay}, 4);
        for (unsigned operation : {1U, 2U})
            invalid_timer("svc_access_stopwatch", {0, operation, 0}, 3);
        for (unsigned bad = 2; bad <= 255; ++bad) {
            setup(2, {uint8_t(bad)});
            status = 4;
            call("svc_timer_active", {0}, 4, true);
            assert(timer_writes == std::vector<uint8_t>({0x20, 0}));
            assert(timer_reads == 1 && reads == 0 && resets == 0 && status == 4);
            assert(output == "\r\r\nService I/O error 4\r\r\n");
        }
        setup(1, {});
        call("svc_start_timer", {0, 1}, 0, false, false);
        setup(2, {0});
        call("svc_timer_active", {0}, 0, false, false);
        setup(2, bytes);
        call("svc_access_stopwatch", {0, 2, 0xa000}, 4, false, false);
    }

    if (symbols.count(symbol_key("io_write_request"))) {
        setup(3, {});
        status = 3;
        call("io_write_request", {0, 0}, -65536);
        assert(request.empty());
        call("io_read_response", {0, 0}, -65536);
        call("io_drain_response", {0}, -65536);
        assert(frame_pos == 0);
    }
    std::printf("PASS bytes=%zu polling=%llu transfer=%llu\n", image.size(),
                (unsigned long long)polling, (unsigned long long)transfer);
    std::printf("TIMERS start_ms=%llu start_seconds=%llu query_expired=%llu query_running=%llu "
                "watch_reset=%llu watch_seconds=%llu watch_ms=%llu\n",
                (unsigned long long)start_ms, (unsigned long long)start_seconds,
                (unsigned long long)query_cycles[0], (unsigned long long)query_cycles[1],
                (unsigned long long)watch_cycles[0], (unsigned long long)watch_cycles[1],
                (unsigned long long)watch_cycles[2]);
}
