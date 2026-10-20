#ifdef NDEBUG
#undef NDEBUG
#endif
#include "io_ports.h"
#include "io_service.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static io_service_result_t result;
static bool pending, finished, hold;
static size_t polls, submissions;
static uint8_t interrupt_rate;

uint64_t time_elapsed_ms(void) { return 0; }

bool io_service_init(void)
{
    pending = finished = hold = false;
    polls = submissions = 0;
    return true;
}

void io_service_shutdown(void) { pending = false; }

io_service_error_t io_service_submit(uint8_t command, const uint8_t *data,
                                    size_t length, uint32_t generation)
{
    (void)command;
    if (pending)
        return IO_SERVICE_BUSY_ERROR;
    assert(length > 0 && length <= IO_SERVICE_MAX_PAYLOAD);
    ++submissions;
    pending = true;
    finished = !hold;
    result.generation = generation;
    result.error = IO_SERVICE_OK;
    result.has_response = true;
    result.length = length;
    memcpy(result.data, data, length);
    return IO_SERVICE_OK;
}

const io_service_result_t *io_service_poll(void)
{
    ++polls;
    if (!pending || !finished)
        return NULL;
    pending = false;
    return &result;
}

void interrupt_timer_output(uint8_t data) { interrupt_rate = data; }
uint8_t interrupt_timer_input(void) { return interrupt_rate; }
size_t files_output(int port, uint8_t data, char *buffer, size_t length)
{
    (void)port; (void)data; (void)buffer; (void)length;
    return 0;
}
uint8_t files_input(uint8_t port) { return port; }

static void begin(size_t length)
{
    io_port_out(IO_SERVICE_COMMAND_PORT, IO_SERVICE_UTILITY);
    io_port_out(IO_SERVICE_REQUEST_PORT, (uint8_t)length);
    io_port_out(IO_SERVICE_REQUEST_PORT, (uint8_t)(length >> 8));
}

static void send(size_t length)
{
    begin(length);
    for (size_t i = 0; i < length; ++i)
        io_port_out(IO_SERVICE_REQUEST_PORT, (uint8_t)i);
}

static void response(size_t length)
{
    assert(io_port_in(IO_SERVICE_RESPONSE_PORT) == (uint8_t)length);
    assert(io_port_in(IO_SERVICE_RESPONSE_PORT) == (uint8_t)(length >> 8));
    for (size_t i = 0; i < length; ++i)
        assert(io_port_in(IO_SERVICE_RESPONSE_PORT) == (uint8_t)i);
}

static void test_poll_placement(void)
{
    assert(io_ports_init());
    send(IO_SERVICE_MAX_PAYLOAD);
    assert(polls == 2 && submissions == 1);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_RESPONSE_READY);
    size_t before = polls;
    response(IO_SERVICE_MAX_PAYLOAD);
    assert(polls == before);
    io_port_out(IO_INTERRUPT_TIMER_PORT, 42);
    assert(io_port_in(IO_INTERRUPT_TIMER_PORT) == 42);
    io_port_out(IO_FILE_COMMAND_PORT, 1);
    io_port_out(IO_FILE_DATA_PORT, 2);
    assert(io_port_in(IO_FILE_COMMAND_PORT) == 60);
    assert(io_port_in(IO_FILE_DATA_PORT) == 61);
    io_port_out(200, 1);
    assert(io_port_in(200) == 0 && polls == before);
    io_port_out(IO_TIMER_PORT, IO_TIMER_START);
    io_port_out(IO_TIMER_PORT, 100);
    io_port_out(IO_TIMER_PORT, 0);
    io_port_out(IO_TIMER_PORT, IO_TIMER_QUERY);
    for (unsigned i = 0; i < 1000; ++i)
        assert(io_port_in(IO_TIMER_PORT) == 1);
    assert(polls == before && submissions == 1);
    io_ports_shutdown();
}

static void test_stale_completion(void)
{
    assert(io_ports_init());
    hold = true;
    send(1);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_BUSY);
    io_port_out(IO_SERVICE_COMMAND_PORT, IO_SERVICE_RESET);
    begin(2);
    io_port_out(IO_SERVICE_REQUEST_PORT, 0);
    /* The abandoned job finishes between payload bytes, with no status read. */
    finished = true;
    hold = false;
    io_port_out(IO_SERVICE_REQUEST_PORT, 1);
    assert(submissions == 2);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_RESPONSE_READY);
    response(2);

    hold = true;
    send(1);
    io_port_out(IO_SERVICE_COMMAND_PORT, IO_SERVICE_RESET);
    io_port_out(IO_SERVICE_COMMAND_PORT, IO_SERVICE_IDENTIFY);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_RESPONSE_READY);
    assert(io_port_in(IO_SERVICE_RESPONSE_PORT) == 13);
    assert(io_port_in(IO_SERVICE_RESPONSE_PORT) == 0);
    const char *identity = "ROMLESS-SVC/1";
    for (size_t i = 0; i < 13; ++i)
    {
        if (i == 5)
            finished = true;
        assert(io_port_in(IO_SERVICE_RESPONSE_PORT) == (uint8_t)identity[i]);
    }
    assert(pending);
    hold = false;
    send(1);
    /* Reading the response directly must publish completion without IN 2. */
    response(1);
    io_ports_shutdown();

    assert(io_ports_init());
    hold = true;
    send(1);
    io_port_out(IO_SERVICE_COMMAND_PORT, IO_SERVICE_RESET);
    send(1);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_ERROR);
    assert(io_port_in(IO_SERVICE_RESPONSE_PORT) == 1);
    assert(io_port_in(IO_SERVICE_RESPONSE_PORT) == 0);
    assert(io_port_in(IO_SERVICE_RESPONSE_PORT) == IO_SERVICE_BUSY_ERROR);
    /* A drained error with reset_required must not become ACK-able. */
    finished = true;
    io_port_out(IO_SERVICE_COMMAND_PORT, IO_SERVICE_ACK);
    assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_ERROR);
    io_port_out(IO_SERVICE_COMMAND_PORT, IO_SERVICE_RESET);
    hold = false;
    send(1);
    response(1);
    io_ports_shutdown();

    assert(io_ports_init());
    hold = true;
    send(1);
    finished = true;
    size_t before = polls;
    io_port_out(IO_INTERRUPT_TIMER_PORT, 25);
    assert(io_port_in(IO_INTERRUPT_TIMER_PORT) == 25);
    io_port_out(IO_FILE_DATA_PORT, 'x');
    assert(pending && polls == before);
    response(1);
    assert(!pending);
    io_ports_shutdown();
}

static uint64_t now_ns(void)
{
    struct timespec now;
    assert(timespec_get(&now, TIME_UTC) == TIME_UTC);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void benchmark(void)
{
    const size_t lengths[] = {1, 64, IO_SERVICE_MAX_PAYLOAD};
    const size_t iterations[] = {1000000, 100000, 20000};
    for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n)
    {
        uint64_t samples[7];
        size_t calls = 0;
        for (size_t sample = 0; sample < 7; ++sample)
        {
            assert(io_ports_init());
            uint64_t start = now_ns();
            for (size_t i = 0; i < iterations[n]; ++i)
            {
                send(lengths[n]);
                assert(io_port_in(IO_SERVICE_COMMAND_PORT) == IO_SERVICE_RESPONSE_READY);
                response(lengths[n]);
            }
            samples[sample] = now_ns() - start;
            calls = polls / iterations[n];
            io_ports_shutdown();
        }
        for (size_t i = 1; i < 7; ++i)
            for (size_t j = i; j > 0 && samples[j] < samples[j - 1]; --j)
            {
                uint64_t swap = samples[j];
                samples[j] = samples[j - 1];
                samples[j - 1] = swap;
            }
        printf("%zu-byte request/reply: median %.1f ns/transaction, %zu worker polls\n",
               lengths[n], (double)samples[3] / iterations[n], calls);
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--benchmark") == 0)
        benchmark();
    else
    {
        assert(argc == 1);
        test_poll_placement();
        test_stale_completion();
        puts("Transport hot-path polling and stale-completion checks passed");
    }
    return 0;
}
