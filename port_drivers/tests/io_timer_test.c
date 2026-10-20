#ifdef NDEBUG
#undef NDEBUG
#endif
#include "io_ports.h"
#include "io_service.h"
#include "../../Apps/COMMON/DXSVC.H"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t clock_ms;
static unsigned polls, submissions, inputs, outputs;

uint64_t time_elapsed_ms(void) { return clock_ms; }
bool io_service_init(void) { return true; }
void io_service_shutdown(void) {}
const io_service_result_t *io_service_poll(void) { ++polls; return NULL; }
io_service_error_t io_service_submit(uint8_t command, const uint8_t *data,
                                    size_t length, uint32_t generation)
{
    (void)command; (void)data; (void)length; (void)generation;
    ++submissions;
    return IO_SERVICE_OK;
}
void interrupt_timer_output(uint8_t value) { (void)value; }
uint8_t interrupt_timer_input(void) { return 0; }
size_t files_output(int port, uint8_t data, char *buffer, size_t length)
{
    (void)port; (void)data; (void)buffer; (void)length;
    return 0;
}
uint8_t files_input(uint8_t port) { (void)port; return 0; }

void outp(unsigned port, unsigned value)
{
    assert(port == TIMER_IO_PORT);
    ++outputs;
    io_port_out((uint8_t)port, (uint8_t)value);
}

int inp(unsigned port)
{
    assert(port == TIMER_IO_PORT);
    ++inputs;
    return io_port_in((uint8_t)port);
}

static uint32_t read_watch(uint8_t command, bool advance_clock)
{
    uint32_t value = 0;
    io_port_out(IO_TIMER_PORT, command);
    for (unsigned i = 0; i < 4; ++i)
    {
        value |= (uint32_t)io_port_in(IO_TIMER_PORT) << (8 * i);
        if (advance_clock)
            clock_ms += 10000;
    }
    return value;
}

static void send(uint8_t group, const uint8_t *data, size_t length)
{
    assert(io_port_in(2) == IO_SERVICE_READY);
    io_port_out(2, group);
    io_port_out(3, (uint8_t)length);
    io_port_out(3, (uint8_t)(length >> 8));
    for (size_t i = 0; i < length; ++i)
        io_port_out(3, data[i]);
}

static void test_countdowns(void)
{
    static const unsigned delays[] = {0, 1, 255, 256, 0x6013, 65535};
    assert(io_ports_init());
    assert(io_port_in(IO_TIMER_PORT) == 0);
    for (int id = 0; id < 4; ++id)
    {
        for (unsigned i = 0; i < sizeof(delays) / sizeof(delays[0]); ++i)
        {
            unsigned delay = delays[i];
            if (id == 3 && delay > 255)
                continue;
            clock_ms = 1234;
            inputs = outputs = 0;
            assert(svc_start_timer(id, delay) == 0);
            assert(outputs == (id == 3 ? 2u : 3u) && inputs == 0);
            assert(svc_timer_active(id) == (delay != 0));
            assert(inputs == 1 && outputs == (id == 3 ? 3u : 4u));
            if (delay)
            {
                clock_ms = id == 3 ? (1 + delay) * 1000 - 1 : 1234 + delay - 1;
                assert(svc_timer_active(id) == 1);
                ++clock_ms;
                assert(svc_timer_active(id) == 0);
            }
        }
    }
    assert(polls == 0 && submissions == 0);

    clock_ms = 50000;
    for (int id = 0; id < 4; ++id)
        svc_start_timer(id, 100);
    io_port_out(IO_TIMER_PORT, IO_TIMER_MASK);
    assert(io_port_in(IO_TIMER_PORT) == 15);
    clock_ms += 100;
    assert(io_port_in(IO_TIMER_PORT) == 8);

    /* Partial writes neither arm nor replace a running timer. */
    svc_start_timer(0, 100);
    io_port_out(IO_TIMER_PORT, IO_TIMER_START);
    io_port_out(IO_TIMER_PORT, 0);
    assert(io_port_in(IO_TIMER_PORT) == IO_TIMER_ERROR);
    io_port_out(IO_TIMER_PORT, IO_TIMER_RESET);
    assert(svc_timer_active(0) == 1);
    clock_ms += 100;
    assert(svc_timer_active(0) == 0);
    io_port_out(IO_TIMER_PORT, IO_TIMER_START);
    assert(io_port_in(IO_TIMER_PORT) == IO_TIMER_ERROR);
    io_port_out(IO_TIMER_PORT, IO_TIMER_RESET);
    assert(svc_timer_active(0) == 0);
}

static void test_stopwatches(void)
{
    uint8_t bytes[4];
    assert(io_ports_init());
    clock_ms = 123456;
    assert(read_watch(IO_STOPWATCH_MS, false) == 123456);
    for (int id = 0; id < 3; ++id)
    {
        inputs = outputs = 0;
        assert(svc_access_stopwatch(id, 0, NULL) == 0 && outputs == 1 && inputs == 0);
        clock_ms += 0x12345678;
        assert(svc_access_stopwatch(id, 2, bytes) == 4);
        assert(memcmp(bytes, "\x78\x56\x34\x12", 4) == 0);
        assert(inputs == 4 && outputs == 2);
        assert(read_watch(IO_STOPWATCH_SECONDS + id, true) == 0x12345678 / 1000);
        assert(io_port_in(IO_TIMER_PORT) == IO_TIMER_ERROR);
        io_port_out(IO_TIMER_PORT, IO_TIMER_RESET);
        assert(svc_access_stopwatch(id, 0, NULL) == 0);
        clock_ms += 0x100000005ULL;
        assert(read_watch(IO_STOPWATCH_MS + id, false) == 5);
        svc_access_stopwatch(id, 0, NULL);
        clock_ms += 0x100000007ULL * 1000;
        assert(read_watch(IO_STOPWATCH_SECONDS + id, false) == 7);
    }
    for (unsigned command = 0; command < 256; ++command)
    {
        bool valid = command == IO_TIMER_RESET || command == IO_TIMER_MASK ||
            (command >= 0x10 && command <= 0x13) ||
            (command >= 0x20 && command <= 0x23) ||
            (command >= 0x40 && command <= 0x42) ||
            (command >= 0x50 && command <= 0x52) ||
            (command >= 0x60 && command <= 0x62);
        if (valid)
            continue;
        io_port_out(IO_TIMER_PORT, (uint8_t)command);
        assert(io_port_in(IO_TIMER_PORT) == IO_TIMER_ERROR);
        io_port_out(IO_TIMER_PORT, IO_TIMER_QUERY);
        assert(io_port_in(IO_TIMER_PORT) == IO_TIMER_ERROR);
        io_port_out(IO_TIMER_PORT, IO_TIMER_RESET);
        assert(io_port_in(IO_TIMER_PORT) <= 15);
    }
}

static void test_shared_state_and_independence(void)
{
    assert(io_ports_init());
    clock_ms = 100;
    const uint8_t start[] = {0, 1, 200, 0, 0, 0};
    send(IO_SERVICE_TIMERS, start, sizeof(start));
    assert(io_port_in(2) == IO_SERVICE_COMPLETE && submissions == 0);
    assert(svc_timer_active(1) == 1);
    io_port_out(2, IO_SERVICE_ACK);
    svc_start_timer(1, 0);
    const uint8_t query[] = {1, 1};
    send(IO_SERVICE_TIMERS, query, sizeof(query));
    assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY);
    assert(io_port_in(4) == 1 && io_port_in(4) == 0 && io_port_in(4) == 0);

    svc_access_stopwatch(2, 0, NULL);
    clock_ms += 1234;
    const uint8_t watch[] = {2, 2};
    send(IO_SERVICE_STOPWATCHES, watch, sizeof(watch));
    assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY);
    assert(io_port_in(4) == 4 && io_port_in(4) == 0);
    assert(io_port_in(4) == 0xd2 && io_port_in(4) == 4);
    assert(io_port_in(4) == 0 && io_port_in(4) == 0);
    const uint8_t reset_watch[] = {0, 2};
    send(IO_SERVICE_STOPWATCHES, reset_watch, sizeof(reset_watch));
    assert(io_port_in(2) == IO_SERVICE_COMPLETE);
    assert(read_watch(IO_STOPWATCH_MS + 2, false) == 0);
    io_port_out(2, IO_SERVICE_ACK);

    const uint8_t utility[] = {0};
    send(IO_SERVICE_UTILITY, utility, sizeof(utility));
    assert(io_port_in(2) == IO_SERVICE_BUSY && submissions == 1);
    unsigned before = polls;
    svc_start_timer(0, 100);
    svc_access_stopwatch(0, 0, NULL);
    clock_ms += 99;
    assert(svc_timer_active(0) == 1);
    assert(read_watch(IO_STOPWATCH_MS, false) == 99);
    ++clock_ms;
    assert(svc_timer_active(0) == 0 && polls == before);
    io_port_out(2, IO_SERVICE_RESET);
    assert(read_watch(IO_STOPWATCH_MS, false) == 100);
    io_ports_shutdown();
}

int main(void)
{
    test_countdowns();
    test_stopwatches();
    test_shared_state_and_independence();
    puts("Dedicated timer port, shared state, client access counts and recovery passed");
    return 0;
}
