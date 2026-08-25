#include "dcc_debug_io_adapter.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

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

    assert(argc == 3);
    config.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    config.struct_size = sizeof(config);
    config.environment_file = argv[1];
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
    adapter.output(adapter.context, 52, 200);
    assert(adapter.input(adapter.context, 52) == 200);
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
    assert(host.clears >= 2);
    puts("I/O adapter lifecycle passed");
    return 0;
}