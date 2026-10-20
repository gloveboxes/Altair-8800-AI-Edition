#include "io_ports.h"
#include "io_service.h"
#include "interrupt_timer.h"
#include "time_io.h"

#include "files_io.h"

#include <string.h>

static struct
{
    io_service_status_t status;
    uint8_t command;
    uint8_t length_bytes;
    uint16_t request_length;
    size_t request_position;
    size_t response_length;
    size_t response_position;
    uint32_t generation;
    bool reset_required;
    uint8_t request[IO_SERVICE_MAX_PAYLOAD];
    uint8_t response[IO_SERVICE_MAX_PAYLOAD];
} transport;

/* Both timer interfaces run on the CPU thread; the service worker never
 * accesses these deadlines or the dedicated port's parser/latch. */
static struct
{
    uint64_t deadlines[4];
    uint64_t stopwatches[3];
    uint32_t latched;
    uint16_t duration;
    uint8_t command;
    uint8_t parameter_bytes;
    uint8_t response_bytes;
} timers;

static void fail(io_service_error_t error, bool reset_required);

static void start_timer(uint8_t id, uint16_t duration)
{
    uint64_t now = time_elapsed_ms();
    timers.deadlines[id] = id == 3 ? (now / 1000 + duration) * 1000
                                   : now + duration;
}

static uint8_t timer_running(uint8_t id, uint64_t now)
{
    return now < timers.deadlines[id];
}

static uint32_t stopwatch_value(uint8_t id, bool milliseconds)
{
    uint64_t elapsed = time_elapsed_ms() - timers.stopwatches[id];
    return (uint32_t)(milliseconds ? elapsed : elapsed / 1000);
}

static void timer_out(uint8_t data)
{
    if (timers.parameter_bytes)
    {
        uint8_t id = timers.command - IO_TIMER_START;
        if (id != 3 && timers.parameter_bytes == 2)
            timers.duration = data;
        else
        {
            timers.duration |= (uint16_t)data << (id == 3 ? 0 : 8);
            start_timer(id, timers.duration);
        }
        if (--timers.parameter_bytes == 0)
            timers.command = IO_TIMER_MASK;
        return;
    }
    if (data == IO_TIMER_RESET)
    {
        timers.command = IO_TIMER_MASK;
        timers.response_bytes = 0;
        return;
    }
    if (timers.command == IO_TIMER_ERROR)
        return;
    timers.command = data;
    timers.response_bytes = 0;
    if (data >= IO_TIMER_START && data <= IO_TIMER_START + 3)
    {
        timers.duration = 0;
        timers.parameter_bytes = data == IO_TIMER_START + 3 ? 1 : 2;
    }
    else if ((data >= IO_TIMER_QUERY && data <= IO_TIMER_QUERY + 3) ||
             data == IO_TIMER_MASK)
        return;
    else if (data >= IO_STOPWATCH_RESET && data <= IO_STOPWATCH_RESET + 2)
    {
        timers.stopwatches[data - IO_STOPWATCH_RESET] = time_elapsed_ms();
        timers.command = IO_TIMER_MASK;
    }
    else if ((data >= IO_STOPWATCH_SECONDS && data <= IO_STOPWATCH_SECONDS + 2) ||
             (data >= IO_STOPWATCH_MS && data <= IO_STOPWATCH_MS + 2))
    {
        timers.latched = stopwatch_value(data & 3, data >= IO_STOPWATCH_MS);
        timers.response_bytes = 4;
    }
    else
        timers.command = IO_TIMER_ERROR;
}

static uint8_t timer_in(void)
{
    if (timers.parameter_bytes)
    {
        timers.parameter_bytes = 0;
        timers.command = IO_TIMER_ERROR;
    }
    if (timers.command >= IO_TIMER_QUERY &&
        timers.command <= IO_TIMER_QUERY + 3)
        return timer_running(timers.command - IO_TIMER_QUERY, time_elapsed_ms());
    if (timers.command == IO_TIMER_MASK)
    {
        uint8_t mask = 0;
        uint64_t now = time_elapsed_ms();
        for (uint8_t id = 0; id < 4; ++id)
            mask |= timer_running(id, now) << id;
        return mask;
    }
    if (timers.response_bytes)
    {
        uint8_t byte = (uint8_t)timers.latched;
        timers.latched >>= 8;
        --timers.response_bytes;
        return byte;
    }
    timers.command = IO_TIMER_ERROR;
    return IO_TIMER_ERROR;
}

static void timer_service(void)
{
    const uint8_t *request = transport.request;
    uint8_t operation = request[0];
    bool countdown = transport.command == IO_SERVICE_TIMERS;
    if (operation > (countdown ? 1 : 2))
    {
        fail(IO_SERVICE_UNKNOWN_OPERATION, false);
        return;
    }
    if (transport.request_length != (countdown && operation == 0 ? 6 : 2))
    {
        fail(IO_SERVICE_INVALID_LENGTH, false);
        return;
    }
    uint8_t id = request[1];
    if (id > (countdown ? 3 : 2))
    {
        fail(IO_SERVICE_INVALID_PARAMETER, false);
        return;
    }
    transport.response_length = 0;
    transport.response_position = 0;
    if (countdown && operation == 0)
    {
        if (request[4] || request[5] || (id == 3 && request[3]))
        {
            fail(IO_SERVICE_INVALID_PARAMETER, false);
            return;
        }
        start_timer(id, (uint16_t)request[2] | ((uint16_t)request[3] << 8));
    }
    else if (countdown)
    {
        transport.response[0] = timer_running(id, time_elapsed_ms());
        transport.response_length = 1;
    }
    else if (operation == 0)
        timers.stopwatches[id] = time_elapsed_ms();
    else
    {
        uint32_t value = stopwatch_value(id, operation == 2);
        for (unsigned i = 0; i < 4; ++i)
            transport.response[i] = (uint8_t)(value >> (8 * i));
        transport.response_length = 4;
    }
    transport.status = transport.response_length ? IO_SERVICE_RESPONSE_READY
                                                : IO_SERVICE_COMPLETE;
}

static void reset_transport(void)
{
    ++transport.generation;
    transport.status = IO_SERVICE_READY;
    transport.length_bytes = 0;
    transport.request_length = 0;
    transport.request_position = 0;
    transport.response_length = 0;
    transport.response_position = 0;
    transport.reset_required = false;
}

static void fail(io_service_error_t error, bool reset_required)
{
    ++transport.generation;
    transport.response[0] = (uint8_t)error;
    transport.response_length = 1;
    transport.response_position = 0;
    transport.reset_required |= reset_required;
    transport.status = IO_SERVICE_ERROR;
}

static void poll_service(void)
{
    const io_service_result_t *result = io_service_poll();
    if (!result || result->generation != transport.generation ||
        transport.status != IO_SERVICE_BUSY)
        return;

    if (result->error != IO_SERVICE_OK)
    {
        fail(result->error, false);
        return;
    }
    if (result->length > sizeof(transport.response))
    {
        fail(IO_SERVICE_CAPACITY, false);
        return;
    }
    memcpy(transport.response, result->data, result->length);
    transport.response_length = result->length;
    transport.response_position = 0;
    transport.status = result->has_response
                           ? IO_SERVICE_RESPONSE_READY : IO_SERVICE_COMPLETE;
}

bool io_ports_init(void)
{
    reset_transport();
    memset(&timers, 0, sizeof(timers));
    timers.command = IO_TIMER_MASK;
    return io_service_init();
}

void io_ports_shutdown(void)
{
    io_service_shutdown();
    reset_transport();
}

static void command_out(uint8_t command)
{
    if (command == IO_SERVICE_RESET)
    {
        reset_transport();
        return;
    }
    if (command == IO_SERVICE_ACK)
    {
        if (!transport.reset_required &&
            (transport.status == IO_SERVICE_COMPLETE ||
             (transport.status == IO_SERVICE_ERROR &&
              transport.response_position == transport.response_length + 2)))
            reset_transport();
        else
            fail(IO_SERVICE_INVALID_STATE, transport.reset_required);
        return;
    }
    if (transport.status != IO_SERVICE_READY)
    {
        fail(IO_SERVICE_BUSY_ERROR, true);
        return;
    }
    if (command == IO_SERVICE_IDENTIFY)
    {
        static const char identity[] = "ROMLESS-SVC/1";
        memcpy(transport.response, identity, sizeof(identity) - 1);
        transport.response_length = sizeof(identity) - 1;
        transport.response_position = 0;
        transport.status = IO_SERVICE_RESPONSE_READY;
        return;
    }
    switch (command)
    {
        case IO_SERVICE_TIMERS:
        case IO_SERVICE_STOPWATCHES:
        case IO_SERVICE_CLOCK:
        case IO_SERVICE_UTILITY:
        case IO_SERVICE_ENVIRONMENT:
        case IO_SERVICE_WEATHER:
        case IO_SERVICE_CHAT:
        case IO_SERVICE_SENSE_HAT:
            transport.command = command;
            transport.length_bytes = 0;
            transport.request_length = 0;
            transport.request_position = 0;
            transport.status = IO_SERVICE_COLLECTING;
            break;
        default:
            fail(IO_SERVICE_UNKNOWN_GROUP, false);
            break;
    }
}

static void request_out(uint8_t data)
{
    if (transport.status != IO_SERVICE_COLLECTING)
    {
        fail(IO_SERVICE_INVALID_STATE, transport.status == IO_SERVICE_BUSY);
        return;
    }
    if (transport.length_bytes < 2)
    {
        transport.request_length |= (uint16_t)data << (8 * transport.length_bytes++);
        if (transport.length_bytes == 2 &&
            (transport.request_length == 0 ||
             transport.request_length > IO_SERVICE_MAX_PAYLOAD))
            fail(IO_SERVICE_INVALID_LENGTH, false);
        return;
    }
    transport.request[transport.request_position++] = data;
    if (transport.request_position == transport.request_length)
    {
        /* RESET can leave an old worker job completing during collection. */
        poll_service();
        if (transport.command == IO_SERVICE_TIMERS ||
            transport.command == IO_SERVICE_STOPWATCHES)
        {
            timer_service();
            return;
        }
        io_service_error_t error = io_service_submit(
            transport.command, transport.request, transport.request_length,
            transport.generation);
        if (error == IO_SERVICE_OK)
            transport.status = IO_SERVICE_BUSY;
        else
            fail(error, error == IO_SERVICE_BUSY_ERROR);
    }
}

void io_port_out(uint8_t port, uint8_t data)
{
    switch (port)
    {
        case IO_TIMER_PORT:
            timer_out(data);
            break;
        case IO_SERVICE_COMMAND_PORT:
            poll_service();
            command_out(data);
            break;
        case IO_SERVICE_REQUEST_PORT:
            if (transport.status != IO_SERVICE_COLLECTING)
                poll_service();
            request_out(data);
            break;
        case IO_INTERRUPT_TIMER_PORT:
            interrupt_timer_output(data);
            break;
        case IO_FILE_COMMAND_PORT:
        case IO_FILE_DATA_PORT:
            /* Driver selectors are private, not externally decoded ports. */
            files_output(port == IO_FILE_COMMAND_PORT ? 60 : 61, data, NULL, 0);
            break;
        default:
            break;
    }
}

uint8_t io_port_in(uint8_t port)
{
    switch (port)
    {
        case IO_TIMER_PORT:
            return timer_in();
        case IO_SERVICE_COMMAND_PORT:
            poll_service();
            return (uint8_t)transport.status;
        case IO_SERVICE_RESPONSE_PORT:
        {
            if (transport.status == IO_SERVICE_BUSY)
                poll_service();
            if ((transport.status != IO_SERVICE_RESPONSE_READY &&
                 transport.status != IO_SERVICE_ERROR) ||
                transport.response_position >= transport.response_length + 2)
            {
                fail(IO_SERVICE_UNDERFLOW, transport.status == IO_SERVICE_BUSY);
                return 0;
            }
            size_t position = transport.response_position++;
            uint8_t data = position < 2
                               ? (uint8_t)(transport.response_length >> (8 * position))
                               : transport.response[position - 2];
            if (transport.status == IO_SERVICE_RESPONSE_READY &&
                transport.response_position == transport.response_length + 2)
                reset_transport();
            return data;
        }
        case IO_INTERRUPT_TIMER_PORT:
            return interrupt_timer_input();
        case IO_FILE_COMMAND_PORT:
        case IO_FILE_DATA_PORT:
            return files_input(port == IO_FILE_COMMAND_PORT ? 60 : 61);
        default:
            return 0;
    }
}
