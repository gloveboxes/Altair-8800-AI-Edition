/* Test-only C reference for native port mocks and Z80 cycle comparisons.
 * Production DXSVC.C contains its own mandatory assembly implementation.
 */
#include "DXSVC.H"

int inp(unsigned port);
void outp(unsigned port, unsigned value);
void io_abort_timer(int error_code);

int io_wait_completion(void)
{
    int batch, status;
    unsigned remaining_polls;
    for (batch = 100; batch != 0; --batch) {
        remaining_polls = 65535U;
        do {
            status = inp(SERVICE_COMMAND_PORT);
            if (status != 4)
                return status;
        } while (--remaining_polls != 0);
    }
    return 4;
}

void io_write_request(const void *request, unsigned byte_count)
{
    const unsigned char *bytes = request;
    while (byte_count != 0) {
        outp(SERVICE_REQUEST_PORT, *bytes++);
        --byte_count;
    }
}

void io_read_response(void *response, unsigned byte_count)
{
    unsigned char *bytes = response;
    while (byte_count != 0) {
        *bytes++ = inp(SERVICE_RESPONSE_PORT);
        --byte_count;
    }
}

void io_drain_response(unsigned byte_count)
{
    while (byte_count != 0) {
        inp(SERVICE_RESPONSE_PORT);
        --byte_count;
    }
}

int svc_start_timer(int timer_id, unsigned duration)
{
    if (timer_id < 0 || timer_id > 3 || (timer_id == 3 && duration > 255))
        io_abort_timer(4);
    outp(TIMER_IO_PORT, 16 + timer_id);
    outp(TIMER_IO_PORT, duration & 255);
    if (timer_id != 3)
        outp(TIMER_IO_PORT, duration >> 8);
    return 0;
}

int svc_timer_active(int timer_id)
{
    int running;
    if (timer_id < 0 || timer_id > 3)
        io_abort_timer(4);
    outp(TIMER_IO_PORT, 32 + timer_id);
    running = inp(TIMER_IO_PORT);
    if (running != 0 && running != 1) {
        outp(TIMER_IO_PORT, 0);
        io_abort_timer(4);
    }
    return running;
}

int svc_access_stopwatch(int stopwatch_id, int operation, void *response)
{
    unsigned char *bytes = response;
    int remaining_bytes;
    if (stopwatch_id < 0 || stopwatch_id > 2 || operation < 0 || operation > 2)
        io_abort_timer(4);
    if (operation && !response)
        io_abort_timer(3);
    outp(TIMER_IO_PORT, 64 + (operation << 4) + stopwatch_id);
    if (operation == 0)
        return 0;
    for (remaining_bytes = 4; remaining_bytes; --remaining_bytes)
        *bytes++ = inp(TIMER_IO_PORT);
    return 4;
}
