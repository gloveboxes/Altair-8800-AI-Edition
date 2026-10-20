#ifdef NDEBUG
#undef NDEBUG
#endif
#include "io_ports.h"
#include "chat_io.h"
#include "environment_io.h"
#include "time_io.h"
#include "utility_io.h"
#include "weather_io.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

static atomic_bool block_environment;
static atomic_bool environment_entered;
static uint8_t rate;
static uint8_t ft_port;
static uint8_t ft_data;
static uint64_t timer_clock;
static bool chat_available = true;
static uint8_t weather_status = 2;
static uint8_t env_status;
static uint8_t env_bytes[512];
static size_t env_length;
static size_t env_response_length;
static bool panel_active;
static uint8_t panel_selector;
static uint8_t panel_data;
static time_t calendar_time = 1704067200;

time_t time(time_t *destination)
{
    if (destination)
        *destination = calendar_time;
    return calendar_time;
}

static void yield_worker(void)
{
#ifdef _WIN32
    Sleep(0);
#else
    sched_yield();
#endif
}

static uint64_t now_ms(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static uint8_t wait_status(void)
{
    uint64_t deadline = now_ms() + 3000;
    uint8_t status;
    do
    {
        status = io_port_in(IO_SERVICE_COMMAND_PORT);
        if (status != IO_SERVICE_BUSY)
            return status;
        yield_worker();
    } while (now_ms() < deadline);
    assert(!"Service worker timed out");
    return 0;
}

static void reset(void)
{
    io_port_out(IO_SERVICE_COMMAND_PORT, IO_SERVICE_RESET);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_READY);
}

static void send(uint8_t group, const uint8_t *data, size_t length)
{
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_READY);
    io_port_out(IO_SERVICE_COMMAND_PORT, group);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_COLLECTING);
    io_port_out(IO_SERVICE_REQUEST_PORT, (uint8_t)length);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_COLLECTING);
    io_port_out(IO_SERVICE_REQUEST_PORT, (uint8_t)(length >> 8));
    for (size_t i = 0; i < length; ++i)
        io_port_out(IO_SERVICE_REQUEST_PORT, data[i]);
}

static size_t response(uint8_t *buffer, size_t capacity)
{
    size_t length = io_port_in(IO_SERVICE_RESPONSE_PORT);
    length |= (size_t)io_port_in(IO_SERVICE_RESPONSE_PORT) << 8;
    assert(length <= capacity);
    for (size_t i = 0; i < length; ++i)
        buffer[i] = io_port_in(IO_SERVICE_RESPONSE_PORT);
    return length;
}

static void complete(void)
{
    assert(wait_status() == IO_SERVICE_COMPLETE);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_COMPLETE);
    io_port_out(IO_SERVICE_COMMAND_PORT, IO_SERVICE_ACK);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_READY);
}

static void error(io_service_error_t expected)
{
    uint8_t bytes[64];
    assert(wait_status() == IO_SERVICE_ERROR);
    assert(response(bytes, sizeof(bytes)) == 1);
    assert(bytes[0] == expected);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_ERROR);
    reset();
}

size_t time_output(int port, uint8_t data, char *buffer, size_t length)
{
    (void)data;
    if (port >= 37 && port <= 39 && data != 0)
    {
        assert(length >= 4);
        memcpy(buffer, "\x12\x34\x00\x01", 4);
        return 4;
    }
    if (port == 31 || port >= 41)
    {
        assert(length >= 6);
        memcpy(buffer, "clock", 6);
        return port == 31 ? 6 : 5;
    }
    return 0;
}

uint8_t time_input(uint8_t port)
{
    (void)port;
    return 1;
}

uint64_t time_elapsed_ms(void) { return timer_clock; }

size_t utility_output(int port, uint8_t data, char *buffer, size_t length)
{
    (void)data;
    assert(length >= 8);
    if (port == 45)
    {
        buffer[0] = 0;
        buffer[1] = (char)0xa5;
        return 2;
    }
    memcpy(buffer, "utility", 8);
    return 7;
}

size_t environment_output(int port, uint8_t data, char *buffer, size_t length)
{
    (void)length;
    if (port == ENVIRONMENT_PORT_DATA)
    {
        env_bytes[env_length++] = data;
        return 0;
    }
    if (data == 0)
        env_length = 0;
    if (data == 1 && atomic_load(&block_environment))
    {
        atomic_store(&environment_entered, true);
        while (atomic_load(&block_environment))
            yield_worker();
    }
    if (buffer)
    {
        assert(env_response_length < length);
        memset(buffer, 'v', env_response_length);
        buffer[env_response_length] = 0;
    }
    return env_response_length;
}

uint8_t environment_input(uint8_t port)
{
    assert(port == ENVIRONMENT_PORT_COMMAND);
    return env_status;
}

size_t weather_output(int port, uint8_t data, char *buffer, size_t length)
{
    assert(port == 46 && data <= 14 && length >= 5);
    if (weather_status == 0)
        return 0;
    memcpy(buffer, "rain", 5);
    return 4;
}

uint8_t weather_input(uint8_t port)
{
    assert(port == 47);
    return weather_status;
}

bool weather_io_has_data(void) { return weather_status != 0; }

bool chat_io_available(void) { return chat_available; }
uint8_t chat_service_submit(void) { return IO_SERVICE_BUSY_ERROR; }
size_t chat_output(int port, uint8_t data, char *buffer, size_t length)
{
    (void)port; (void)data; (void)buffer; (void)length;
    return 0;
}
uint8_t chat_input(uint8_t port)
{
    return port == CHAT_PORT_STATUS ? CHAT_STATUS_EOF : 0;
}

void interrupt_timer_output(uint8_t value) { rate = value; }
uint8_t interrupt_timer_input(void) { return rate; }
size_t files_output(int port, uint8_t data, char *buffer, size_t length)
{
    (void)buffer; (void)length;
    ft_port = (uint8_t)port;
    ft_data = data;
    return 0;
}
uint8_t files_input(uint8_t port) { return port; }

bool sense_hat_panel_is_active(void) { return panel_active; }
size_t sense_hat_panel_output(uint8_t selector, uint8_t data,
                              char *buffer, size_t length)
{
    assert(length >= 3);
    panel_selector = selector;
    panel_data = data;
    memcpy(buffer, "25", 3);
    return 2;
}

static void test_transport(void)
{
    uint8_t bytes[64];
    reset();
    io_port_out(2, IO_SERVICE_IDENTIFY);
    io_port_out(7, 123);
    io_port_out(0x15, 3);
    assert(ft_port == 60 && ft_data == 3);
    io_port_out(0x16, 'A');
    assert(ft_port == 61 && ft_data == 'A');
    assert(io_port_in(7) == 123);
    assert(io_port_in(0x15) == 60 && io_port_in(0x16) == 61);
    assert(response(bytes, sizeof(bytes)) == 13);
    assert(memcmp(bytes, "ROMLESS-SVC/1", 13) == 0);
    assert(io_port_in(2) == IO_SERVICE_READY);
    io_port_out(60, 4);
    io_port_out(52, 0);
    io_port_out(200, 0);
    assert(ft_data == 'A' && rate == 123 && io_port_in(200) == 0);
    assert(io_port_in(2) == IO_SERVICE_READY);
    io_port_out(2, 0xfe);
    error(IO_SERVICE_UNKNOWN_GROUP);
    io_port_out(2, IO_SERVICE_ACK);
    error(IO_SERVICE_INVALID_STATE);
    io_port_in(4);
    error(IO_SERVICE_UNDERFLOW);
    io_port_out(3, 1);
    error(IO_SERVICE_INVALID_STATE);
    io_port_out(2, IO_SERVICE_TIMERS);
    io_port_out(3, 0);
    io_port_out(3, 0);
    error(IO_SERVICE_INVALID_LENGTH);
    io_port_out(2, IO_SERVICE_CHAT);
    io_port_out(3, 1);
    io_port_out(3, 8);
    error(IO_SERVICE_INVALID_LENGTH);
    io_port_out(2, IO_SERVICE_TIMERS);
    io_port_out(3, 6);
    io_port_out(2, IO_SERVICE_CLOCK);
    error(IO_SERVICE_BUSY_ERROR);
    io_port_out(2, IO_SERVICE_IDENTIFY);
    io_port_out(2, IO_SERVICE_CLOCK);
    error(IO_SERVICE_BUSY_ERROR);
}

static void test_dispatch(void)
{
    uint8_t bytes[64];
    uint8_t timer[] = {0, 0, 0x88, 0x13, 0, 0};
    for (uint8_t id = 0; id < 3; ++id)
    {
        timer[1] = id;
        send(IO_SERVICE_TIMERS, timer, sizeof(timer));
        complete();
        io_port_out(IO_TIMER_PORT, IO_TIMER_QUERY + id);
        assert(io_port_in(IO_TIMER_PORT) == 1);
        uint8_t query[] = {1, id};
        send(IO_SERVICE_TIMERS, query, sizeof(query));
        assert(wait_status() == IO_SERVICE_RESPONSE_READY);
        assert(response(bytes, sizeof(bytes)) == 1 && bytes[0] == 1);
    }
    timer[1] = 3; timer[2] = 255; timer[3] = 0;
    send(IO_SERVICE_TIMERS, timer, sizeof(timer)); complete();
    io_port_out(IO_TIMER_PORT, IO_TIMER_QUERY + 3);
    assert(io_port_in(IO_TIMER_PORT) == 1);
    timer[2] = 0; timer[3] = 1;
    send(IO_SERVICE_TIMERS, timer, sizeof(timer)); error(IO_SERVICE_INVALID_PARAMETER);
    timer[1] = 4;
    send(IO_SERVICE_TIMERS, timer, sizeof(timer)); error(IO_SERVICE_INVALID_PARAMETER);
    uint8_t malformed[] = {0, 0};
    send(IO_SERVICE_TIMERS, malformed, sizeof(malformed)); error(IO_SERVICE_INVALID_LENGTH);
    for (uint8_t id = 0; id < 3; ++id)
    {
        io_port_out(IO_TIMER_PORT, IO_STOPWATCH_RESET + id);
        timer_clock += 0x12340001ULL * 1000;
        uint8_t stopwatch[] = {1, id};
        send(IO_SERVICE_STOPWATCHES, stopwatch, sizeof(stopwatch));
        assert(wait_status() == IO_SERVICE_RESPONSE_READY);
        assert(response(bytes, sizeof(bytes)) == 4);
        assert(memcmp(bytes, "\x01\x00\x34\x12", 4) == 0);
    }
    for (uint8_t operation = 0; operation < 5; ++operation)
    {
        send(IO_SERVICE_CLOCK, &operation, 1);
        assert(wait_status() == IO_SERVICE_RESPONSE_READY);
        assert(response(bytes, sizeof(bytes)) == 5);
    }
    calendar_time = 0;
    uint8_t calendar_operation = 1;
    send(IO_SERVICE_CLOCK, &calendar_operation, 1);
    error(IO_SERVICE_CLOCK_UNSYNCED);
    calendar_time = 1704067200;
    uint8_t operation = 0;
    send(IO_SERVICE_UTILITY, &operation, 1);
    assert(wait_status() == IO_SERVICE_RESPONSE_READY);
    assert(response(bytes, sizeof(bytes)) == 2 && bytes[0] == 0 && bytes[1] == 0xa5);
    uint8_t reboot[] = {2, 0};
    send(IO_SERVICE_UTILITY, reboot, 2); error(IO_SERVICE_REBOOT_CONFIRMATION);
    reboot[1] = 0xa5;
    send(IO_SERVICE_UTILITY, reboot, 2); error(IO_SERVICE_UNAVAILABLE);
    uint8_t field[] = {0, 3};
    send(IO_SERVICE_WEATHER, field, 2);
    assert(wait_status() == IO_SERVICE_RESPONSE_READY);
    assert(response(bytes, sizeof(bytes)) == 4);
    for (uint8_t id = 11; id <= 14; ++id)
    {
        field[1] = id;
        send(IO_SERVICE_WEATHER, field, 2);
        assert(wait_status() == IO_SERVICE_RESPONSE_READY);
        assert(response(bytes, sizeof(bytes)) == 4);
    }
    field[1] = 3;
    weather_status = 0;
    send(IO_SERVICE_WEATHER, field, 2); error(IO_SERVICE_UNAVAILABLE);
    uint8_t sensor[] = {63, 0};
    send(IO_SERVICE_SENSE_HAT, sensor, 2); error(IO_SERVICE_UNAVAILABLE);
    panel_active = true;
    send(IO_SERVICE_SENSE_HAT, sensor, 2);
    assert(wait_status() == IO_SERVICE_RESPONSE_READY);
    assert(response(bytes, sizeof(bytes)) == 2);
    assert(memcmp(bytes, "25", 2) == 0 && panel_selector == 63 && panel_data == 0);
    static const uint8_t panel_ports[] = {65, 80, 81, 85, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102};
    for (size_t i = 0; i < sizeof(panel_ports); ++i)
    {
        sensor[0] = panel_ports[i]; sensor[1] = 123;
        send(IO_SERVICE_SENSE_HAT, sensor, 2); complete();
        assert(panel_selector == panel_ports[i] && panel_data == 123);
    }
    uint8_t set[] = {3, 1, 'K', 1, 0, 'V'};
    send(IO_SERVICE_ENVIRONMENT, set, sizeof(set)); complete();
    assert(env_length == 3 && memcmp(env_bytes, "K\0V", 3) == 0);
    uint8_t get[] = {2, 1, 'K'};
    send(IO_SERVICE_ENVIRONMENT, get, sizeof(get));
    assert(wait_status() == IO_SERVICE_RESPONSE_READY);
    assert(response(bytes, sizeof(bytes)) == 0);
    env_status = 251;
    send(IO_SERVICE_ENVIRONMENT, get, sizeof(get)); error(IO_SERVICE_NOT_FOUND);
    env_status = 0;
    get[2] = 0;
    send(IO_SERVICE_ENVIRONMENT, get, sizeof(get)); error(IO_SERVICE_INVALID_PARAMETER);
}

static void test_response_limit(void)
{
    uint8_t operation = 5;
    uint8_t bytes[2048];
    env_response_length = sizeof(bytes);
    send(IO_SERVICE_ENVIRONMENT, &operation, 1);
    assert(wait_status() == IO_SERVICE_RESPONSE_READY);
    assert(response(bytes, sizeof(bytes)) == sizeof(bytes));
    for (size_t i = 0; i < sizeof(bytes); ++i)
        assert(bytes[i] == 'v');
    assert(io_port_in(2) == IO_SERVICE_READY);
    env_response_length = 0;
}

static void test_chat_limits(void)
{
    uint8_t block[2048];
    uint8_t bytes[8];
    uint8_t operation = 0;
    send(IO_SERVICE_CHAT, &operation, 1); complete();
    memset(block, 'x', sizeof(block)); block[0] = 1;
    for (int i = 0; i < 4; ++i)
    {
        send(IO_SERVICE_CHAT, block, sizeof(block));
        complete();
    }
    send(IO_SERVICE_CHAT, block, 4); complete();
    send(IO_SERVICE_CHAT, block, 2); error(IO_SERVICE_CAPACITY);
    uint8_t chunk[] = {5, 0, 1};
    send(IO_SERVICE_CHAT, chunk, sizeof(chunk));
    assert(wait_status() == IO_SERVICE_RESPONSE_READY);
    assert(response(bytes, sizeof(bytes)) == 3);
    assert(bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0);
    operation = 3;
    send(IO_SERVICE_CHAT, &operation, 1); error(IO_SERVICE_BUSY_ERROR);
    chat_available = false;
    operation = 4;
    send(IO_SERVICE_CHAT, &operation, 1); error(IO_SERVICE_UNAVAILABLE);
}

static void test_late_completion(void)
{
    uint8_t operation = 1;
    atomic_store(&block_environment, true);
    send(IO_SERVICE_ENVIRONMENT, &operation, 1);
    uint64_t deadline = now_ms() + 3000;
    while (!atomic_load(&environment_entered) && now_ms() < deadline)
        yield_worker();
    assert(atomic_load(&environment_entered));
    assert(io_port_in(2) == IO_SERVICE_BUSY);
    io_port_out(IO_TIMER_PORT, IO_TIMER_START);
    io_port_out(IO_TIMER_PORT, 100);
    io_port_out(IO_TIMER_PORT, 0);
    io_port_out(IO_TIMER_PORT, IO_TIMER_QUERY);
    assert(io_port_in(IO_TIMER_PORT) == 1);
    io_port_out(IO_TIMER_PORT, IO_STOPWATCH_RESET);
    ++timer_clock;
    io_port_out(IO_TIMER_PORT, IO_STOPWATCH_MS);
    assert(io_port_in(IO_TIMER_PORT) == 1);
    for (unsigned i = 0; i < 3; ++i)
        assert(io_port_in(IO_TIMER_PORT) == 0);
    assert(io_port_in(2) == IO_SERVICE_BUSY);
    reset();
    uint8_t timer_query[] = {1, 0};
    send(IO_SERVICE_TIMERS, timer_query, sizeof(timer_query));
    assert(wait_status() == IO_SERVICE_RESPONSE_READY);
    uint8_t running[1];
    assert(response(running, sizeof(running)) == 1 && running[0] == 1);
    send(IO_SERVICE_CLOCK, &operation, 1);
    error(IO_SERVICE_BUSY_ERROR);
    atomic_store(&block_environment, false);
    /* Drain the late completion without letting it overwrite IDENTIFY. */
    io_port_out(2, IO_SERVICE_IDENTIFY);
    uint8_t bytes[32];
    while (now_ms() < deadline)
    {
        assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY);
        yield_worker();
    }
    assert(response(bytes, sizeof(bytes)) == 13);
}

static void test_completion_publication(void)
{
    uint8_t operation = 0;
    uint8_t bytes[2];
    for (unsigned i = 0; i < 1000; ++i)
    {
        send(IO_SERVICE_UTILITY, &operation, 1);
        assert(wait_status() == IO_SERVICE_RESPONSE_READY);
        assert(response(bytes, sizeof(bytes)) == 2);
        assert(bytes[0] == 0 && bytes[1] == 0xa5);
    }
}

int main(void)
{
    assert(io_ports_init());
    test_transport();
    test_dispatch();
    test_response_limit();
    test_chat_limits();
    test_late_completion();
    test_completion_publication();
    io_ports_shutdown();
    assert(io_ports_init());
    assert(io_port_in(2) == IO_SERVICE_READY);
    io_ports_shutdown();
    puts("Shared service framing, dispatch, limits and recovery passed");
    return 0;
}
