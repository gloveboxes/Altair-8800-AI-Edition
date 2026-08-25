#include "dcc_debug_io_adapter.h"
#include "dcc_debug_terminal_input.h"

#include "PortDrivers/chat_io.h"
#include "PortDrivers/environment_io.h"
#include "PortDrivers/host_files_io.h"
#include "PortDrivers/time_io.h"
#include "PortDrivers/weather_io.h"
#include "interrupt_timer.h"
#include "io_ports.h"

#include <stdio.h>
#include <string.h>

static int s_initialized;

typedef struct adapter_context
{
    dcc_debug_terminal_input_t terminal;
} adapter_context_t;

static adapter_context_t s_context;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error == NULL || error_size == 0)
        return;
    snprintf(error, error_size, "%s", message);
}

static uint8_t adapter_input(void *context, uint8_t port)
{
    (void)context;
    return io_port_in(port);
}

static void adapter_output(void *context, uint8_t port, uint8_t data)
{
    (void)context;
    io_port_out(port, data);
}

static size_t adapter_terminal_input(
    void *context, const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_size, uint64_t now_ms)
{
    adapter_context_t *adapter = (adapter_context_t *)context;
    return dcc_debug_terminal_input_process(
        &adapter->terminal, input, input_size, output, output_size, now_ms);
}

static size_t adapter_terminal_poll(
    void *context, uint8_t *output, size_t output_size, uint64_t now_ms)
{
    adapter_context_t *adapter = (adapter_context_t *)context;
    return dcc_debug_terminal_input_poll(
        &adapter->terminal, output, output_size, now_ms);
}

static void adapter_close(void *context)
{
    (void)context;
    if (!s_initialized)
        return;
    interrupt_timer_close();
    weather_io_close();
    chat_io_close();
    host_files_close();
    environment_io_close();
    s_initialized = 0;
}

int dcc_debug_io_adapter_init(const dcc_debug_io_adapter_config_t *config,
                              dcc_debug_io_adapter_t *adapter,
                              char *error, size_t error_size)
{
    if (config == NULL || adapter == NULL)
    {
        set_error(error, error_size, "config and adapter are required");
        return 0;
    }
    if (config->abi_version != DCC_DEBUG_IO_ADAPTER_ABI_VERSION ||
        config->struct_size < sizeof(*config) ||
        config->host.abi_version != DCC_DEBUG_IO_ADAPTER_ABI_VERSION ||
        config->host.struct_size < sizeof(config->host))
    {
        set_error(error, error_size, "unsupported I/O adapter ABI version");
        return 0;
    }
    if (config->host.register_interrupt == NULL ||
        config->host.raise_interrupt == NULL ||
        config->host.clear_interrupt == NULL)
    {
        set_error(error, error_size, "interrupt host services are required");
        return 0;
    }
    if (s_initialized)
    {
        set_error(error, error_size, "adapter is already initialized");
        return 0;
    }
    if (!interrupt_timer_init(&config->host))
    {
        set_error(error, error_size, "cannot register interrupt timer");
        return 0;
    }

    dcc_debug_terminal_input_init(&s_context.terminal);
    host_files_init(config->files_root ? config->files_root : ".");
    environment_io_init(config->environment_file);
    chat_io_init();
    weather_io_init();
    time_reset();
    s_initialized = 1;

    memset(adapter, 0, sizeof(*adapter));
    adapter->abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    adapter->struct_size = sizeof(*adapter);
    adapter->context = &s_context;
    adapter->input = adapter_input;
    adapter->output = adapter_output;
    adapter->close = adapter_close;
    adapter->terminal_input = adapter_terminal_input;
    adapter->terminal_poll = adapter_terminal_poll;
    if (error != NULL && error_size != 0)
        error[0] = '\0';
    return 1;
}