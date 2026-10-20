#ifdef NDEBUG
#undef NDEBUG
#endif
#include "dcc_debug_io_adapter.h"
#include "io_ports.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint8_t service_wait(dcc_debug_io_adapter_t *adapter)
{
    time_t deadline = time(NULL) + 5;
    uint8_t status;
    do
    {
        status = adapter->input(adapter->context, IO_SERVICE_COMMAND_PORT);
        if (status != IO_SERVICE_BUSY)
            return status;
    } while (time(NULL) < deadline);
    assert(!"Service timeout");
    return 0;
}

static uint8_t service_send(dcc_debug_io_adapter_t *adapter, uint8_t group,
                            const uint8_t *payload, size_t length)
{
    assert(adapter->input(adapter->context, IO_SERVICE_COMMAND_PORT) == IO_SERVICE_READY);
    adapter->output(adapter->context, IO_SERVICE_COMMAND_PORT, group);
    adapter->output(adapter->context, IO_SERVICE_REQUEST_PORT, (uint8_t)length);
    adapter->output(adapter->context, IO_SERVICE_REQUEST_PORT, (uint8_t)(length >> 8));
    for (size_t i = 0; i < length; ++i)
        adapter->output(adapter->context, IO_SERVICE_REQUEST_PORT, payload[i]);
    return service_wait(adapter);
}

static size_t service_read(dcc_debug_io_adapter_t *adapter, uint8_t *data,
                            size_t capacity)
{
    size_t length = adapter->input(adapter->context, IO_SERVICE_RESPONSE_PORT);
    length |= (size_t)adapter->input(adapter->context, IO_SERVICE_RESPONSE_PORT) << 8;
    assert(length <= capacity);
    for (size_t i = 0; i < length; ++i)
        data[i] = adapter->input(adapter->context, IO_SERVICE_RESPONSE_PORT);
    return length;
}

static void service_ack(dcc_debug_io_adapter_t *adapter)
{
    adapter->output(adapter->context, IO_SERVICE_COMMAND_PORT, IO_SERVICE_ACK);
    assert(adapter->input(adapter->context, IO_SERVICE_COMMAND_PORT) == IO_SERVICE_READY);
}

static void test_services(dcc_debug_io_adapter_t *adapter, const char *directory)
{
    uint8_t bytes[2048];
    uint8_t set[] = {3, 3, 'K', 'E', 'Y', 0, 0};
    assert(service_send(adapter, IO_SERVICE_ENVIRONMENT, set, sizeof(set)) == IO_SERVICE_COMPLETE);
    service_ack(adapter);
    uint8_t get[] = {2, 3, 'K', 'E', 'Y'};
    assert(service_send(adapter, IO_SERVICE_ENVIRONMENT, get, sizeof(get)) == IO_SERVICE_RESPONSE_READY);
    assert(service_read(adapter, bytes, sizeof(bytes)) == 0);
    uint8_t delete_request[] = {4, 3, 'K', 'E', 'Y'};
    assert(service_send(adapter, IO_SERVICE_ENVIRONMENT, delete_request, sizeof(delete_request)) == IO_SERVICE_COMPLETE);
    service_ack(adapter);
    assert(service_send(adapter, IO_SERVICE_ENVIRONMENT, get, sizeof(get)) == IO_SERVICE_ERROR);
    assert(service_read(adapter, bytes, sizeof(bytes)) == 1 && bytes[0] == IO_SERVICE_NOT_FOUND);
    service_ack(adapter);
    uint8_t timer[] = {0, 0, 0xff, 0xff, 0, 0};
    for (uint8_t id = 0; id < 3; ++id)
    {
        timer[1] = id;
        assert(service_send(adapter, IO_SERVICE_TIMERS, timer, sizeof(timer)) == IO_SERVICE_COMPLETE);
        service_ack(adapter);
        uint8_t query[] = {1, id};
        assert(service_send(adapter, IO_SERVICE_TIMERS, query, sizeof(query)) == IO_SERVICE_RESPONSE_READY);
        assert(service_read(adapter, bytes, sizeof(bytes)) == 1 && bytes[0] == 1);
    }
    uint8_t stopwatch[] = {0, 2};
    assert(service_send(adapter, IO_SERVICE_STOPWATCHES, stopwatch, 2) == IO_SERVICE_COMPLETE);
    service_ack(adapter);
    stopwatch[0] = 1;
    assert(service_send(adapter, IO_SERVICE_STOPWATCHES, stopwatch, 2) == IO_SERVICE_RESPONSE_READY);
    assert(service_read(adapter, bytes, sizeof(bytes)) == 4);
    assert(memcmp(bytes, "\0\0\0\0", 4) == 0);
    adapter->output(adapter->context, IO_TIMER_PORT, IO_TIMER_QUERY);
    assert(adapter->input(adapter->context, IO_TIMER_PORT) == 1);
    adapter->output(adapter->context, IO_TIMER_PORT, IO_TIMER_START);
    adapter->output(adapter->context, IO_TIMER_PORT, 0);
    adapter->output(adapter->context, IO_TIMER_PORT, 0);
    uint8_t direct_query[] = {1, 0};
    assert(service_send(adapter, IO_SERVICE_TIMERS, direct_query, sizeof(direct_query)) == IO_SERVICE_RESPONSE_READY);
    assert(service_read(adapter, bytes, sizeof(bytes)) == 1 && bytes[0] == 0);
    adapter->output(adapter->context, IO_TIMER_PORT, IO_STOPWATCH_RESET + 2);
    adapter->output(adapter->context, IO_TIMER_PORT, IO_STOPWATCH_SECONDS + 2);
    for (unsigned i = 0; i < 4; ++i)
        assert(adapter->input(adapter->context, IO_TIMER_PORT) == 0);
    for (uint8_t operation = 0; operation < 5; ++operation)
    {
        assert(service_send(adapter, IO_SERVICE_CLOCK, &operation, 1) == IO_SERVICE_RESPONSE_READY);
        assert(service_read(adapter, bytes, sizeof(bytes)) > 0);
    }
    uint8_t random = 0;
    assert(service_send(adapter, IO_SERVICE_UTILITY, &random, 1) == IO_SERVICE_RESPONSE_READY);
    assert(service_read(adapter, bytes, sizeof(bytes)) == 2);
    uint8_t long_set[262] = {3, 3, 'K', '0', '0', 255, 0};
    memset(long_set + 7, 'v', 255);
    for (uint8_t i = 0; i < 9; ++i)
    {
        long_set[4] = (uint8_t)('0' + i);
        assert(service_send(adapter, IO_SERVICE_ENVIRONMENT, long_set, sizeof(long_set)) == IO_SERVICE_COMPLETE);
        service_ack(adapter);
    }
    uint8_t list = 5;
    assert(service_send(adapter, IO_SERVICE_ENVIRONMENT, &list, 1) == IO_SERVICE_ERROR);
    assert(service_read(adapter, bytes, sizeof(bytes)) == 1 && bytes[0] == IO_SERVICE_CAPACITY);
    service_ack(adapter);

    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/SERVICE.BIN", directory);
    FILE *file = fopen(file_path, "wb");
    assert(file);
    for (unsigned i = 0; i < 256; ++i)
        assert(fputc((int)i, file) != EOF);
    assert(fclose(file) == 0);
    adapter->output(adapter->context, IO_FILE_COMMAND_PORT, 1);
    const char filename[] = "SERVICE.BIN";
    for (size_t i = 0; i < sizeof(filename); ++i)
        adapter->output(adapter->context, IO_FILE_DATA_PORT, (uint8_t)filename[i]);
    adapter->output(adapter->context, IO_FILE_COMMAND_PORT, 3);
    assert(adapter->input(adapter->context, IO_FILE_COMMAND_PORT) == 1);
    assert(adapter->input(adapter->context, IO_FILE_DATA_PORT) == 0);
    for (unsigned i = 0; i < 256; ++i)
        assert(adapter->input(adapter->context, IO_FILE_DATA_PORT) == (uint8_t)i);
    adapter->output(adapter->context, IO_FILE_COMMAND_PORT, 3);
    assert(adapter->input(adapter->context, IO_FILE_COMMAND_PORT) == 2);
    adapter->output(adapter->context, IO_FILE_COMMAND_PORT, 4);
    assert(remove(file_path) == 0);
}

typedef struct test_host
{
    dcc_debug_io_interrupt_poll_fn poll;
    void *poll_context;
    unsigned registrations;
    unsigned raises;
    unsigned clears;
} test_host_t;

static int register_interrupt(void *context, uint8_t data_bus,
                              dcc_debug_io_interrupt_poll_fn poll,
                              void *poll_context,
                              dcc_debug_io_interrupt_id_t *interrupt_id)
{
    test_host_t *host = (test_host_t *)context;
    assert(data_bus == 0xff);
    host->poll = poll;
    host->poll_context = poll_context;
    ++host->registrations;
    *interrupt_id = 3;
    return 1;
}

static void raise_interrupt(void *context,
                            dcc_debug_io_interrupt_id_t interrupt_id)
{
    test_host_t *host = (test_host_t *)context;
    assert(interrupt_id == 3);
    ++host->raises;
}

static void clear_interrupt(void *context,
                            dcc_debug_io_interrupt_id_t interrupt_id)
{
    test_host_t *host = (test_host_t *)context;
    assert(interrupt_id == 3);
    ++host->clears;
}

static void assert_terminal_sequence(dcc_debug_io_adapter_t *adapter,
                                     const char *sequence, uint8_t expected)
{
    uint8_t output[8] = {0};
    size_t count = adapter->terminal_input(
        adapter->context, (const uint8_t *)sequence, 3,
        output, sizeof(output), 100);
    assert(count == 1);
    assert(output[0] == expected);
}

static void assert_terminal_numeric_sequence(dcc_debug_io_adapter_t *adapter,
                                             char number, uint8_t expected)
{
    uint8_t input[4] = {0x1b, '[', (uint8_t)number, '~'};
    uint8_t output[8] = {0};
    size_t count = adapter->terminal_input(
        adapter->context, input, sizeof(input), output, sizeof(output), 100);
    assert(count == 1);
    assert(output[0] == expected);
}

int main(int argc, char **argv)
{
    test_host_t host = {0};
    dcc_debug_io_adapter_config_t config = {0};
    dcc_debug_io_adapter_t adapter = {0};
    char error[256];
    char environment_path[1024];

    assert(argc == 3);
    config.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    config.struct_size = sizeof(config);
    snprintf(environment_path, sizeof(environment_path), "%s/io-adapter-env-test.txt", argv[2]);
    config.environment_file = environment_path;
    config.files_root = argv[2];
    config.host.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    config.host.struct_size = sizeof(config.host);
    config.host.context = &host;
    config.host.register_interrupt = register_interrupt;
    config.host.raise_interrupt = raise_interrupt;
    config.host.clear_interrupt = clear_interrupt;

    assert(dcc_debug_io_adapter_init(&config, &adapter, error, sizeof(error)));
    assert(adapter.abi_version == DCC_DEBUG_IO_ADAPTER_ABI_VERSION);
    assert(host.registrations == 1);
    assert(host.poll != NULL);
    adapter.output(adapter.context, 0x07, 200);
    assert(adapter.input(adapter.context, 0x07) == 200);
    test_services(&adapter, argv[2]);
    assert(adapter.terminal_input != NULL);
    assert(adapter.terminal_poll != NULL);
    assert_terminal_sequence(&adapter, "\x1b[A", 0x05);
    assert_terminal_sequence(&adapter, "\x1b[B", 0x18);
    assert_terminal_sequence(&adapter, "\x1b[C", 0x04);
    assert_terminal_sequence(&adapter, "\x1b[D", 0x13);
    assert_terminal_numeric_sequence(&adapter, '2', 0x0f);
    assert_terminal_numeric_sequence(&adapter, '3', 0x07);
    assert_terminal_numeric_sequence(&adapter, '5', 0x12);
    assert_terminal_numeric_sequence(&adapter, '6', 0x16);
    {
        uint8_t input = 0x7f;
        uint8_t output[2] = {0};
        assert(adapter.terminal_input(adapter.context, &input, 1,
                                      output, sizeof(output), 100) == 1);
        assert(output[0] == 0x08);
    }
    {
        uint8_t escape = 0x1b;
        uint8_t output[2] = {0};
        assert(adapter.terminal_input(adapter.context, &escape, 1,
                                      output, sizeof(output), 100) == 0);
        assert(adapter.terminal_poll(adapter.context, output,
                                     sizeof(output), 129) == 0);
        assert(adapter.terminal_poll(adapter.context, output,
                                     sizeof(output), 130) == 1);
        assert(output[0] == 0x1b);
    }
    adapter.close(adapter.context);
    assert(remove(environment_path) == 0);
    assert(host.clears >= 2);
    puts("I/O adapter lifecycle passed");
    return 0;
}