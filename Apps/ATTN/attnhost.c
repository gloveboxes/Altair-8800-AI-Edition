#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/* Host adapter for dedicated timers, compatible services and app entry. */
#define main attnc11_main
#include "attnc11.c"
#undef main

static struct timespec stopwatch_started;
static unsigned service_status;
static unsigned request_size;
static unsigned request_bytes;
static unsigned char request[2];
static unsigned char response[6];
static unsigned response_size;
static unsigned response_byte;
static unsigned char timer_reply[4];
static unsigned timer_byte = 4;

static uint32_t stopwatch_value(unsigned operation)
{
    struct timespec now;
    int64_t elapsed;

    timespec_get(&now, TIME_UTC);
    elapsed = ((int64_t)now.tv_sec - stopwatch_started.tv_sec) * 1000;
    elapsed += ((int64_t)now.tv_nsec - stopwatch_started.tv_nsec) / 1000000;
    if (elapsed < 0)
        elapsed = 0;
    return operation == 1 ? (uint32_t)(elapsed / 1000) : (uint32_t)elapsed;
}

static void stopwatch_request(void)
{
    uint32_t value;
    unsigned index;
    if (request_size != 2 || request[1] != 0 || request[0] > 2) {
        response[0] = 1;
        response[1] = 0;
        response[2] = 4;
        response_size = 3;
        service_status = 255;
        return;
    }
    if (request[0] == 0) {
        timespec_get(&stopwatch_started, TIME_UTC);
        service_status = 1;
        return;
    }
    value = stopwatch_value(request[0]);
    response[0] = 4;
    response[1] = 0;
    for (index = 0; index < 4; index++)
        response[index + 2] = (value >> (index * 8)) & 255;
    response_size = 6;
    service_status = 2;
}

void outp(unsigned port, unsigned value)
{
    value &= 255;
    if (port == TIMER_IO_PORT) {
        timer_byte = 4;
        if (value == 0x40)
            timespec_get(&stopwatch_started, TIME_UTC);
        else if (value == 0x50 || value == 0x60) {
            uint32_t elapsed = stopwatch_value(value == 0x50 ? 1 : 2);
            unsigned index;
            for (index = 0; index < 4; ++index)
                timer_reply[index] = (elapsed >> (index * 8)) & 255;
            timer_byte = 0;
        }
        return;
    }
    if (port == SERVICE_COMMAND_PORT) {
        if (value == 0 || value == 1) {
            service_status = 0;
            response_byte = 0;
            response_size = 0;
        } else if (value == SERVICE_GROUP_STOPWATCHES && service_status == 0) {
            service_status = 3;
            request_size = 0;
            request_bytes = 0;
            response_byte = 0;
        } else {
            response[0] = 1;
            response[1] = 0;
            response[2] = 1;
            response_size = 3;
            response_byte = 0;
            service_status = 255;
        }
        return;
    }
    if (port != SERVICE_REQUEST_PORT || service_status != 3)
        return;
    if (request_bytes == 0)
        request_size = value;
    else if (request_bytes == 1)
        request_size |= value << 8;
    else if (request_bytes < 4)
        request[request_bytes - 2] = value;
    request_bytes++;
    if (request_bytes >= 2 && request_bytes == request_size + 2)
        stopwatch_request();
}

int inp(unsigned port)
{
    int value;

    if (port == TIMER_IO_PORT)
        return timer_byte < 4 ? timer_reply[timer_byte++] : 255;
    if (port == SERVICE_COMMAND_PORT)
        return service_status;
    if (port != SERVICE_RESPONSE_PORT || response_byte >= response_size)
        return 0;
    value = response[response_byte++];
    if (response_byte == response_size && service_status == 2)
        service_status = 0;
    return value;
}

int main(int argc, char *argv[])
{
    timespec_get(&stopwatch_started, TIME_UTC);
    return attnc11_main(argc, argv);
}
