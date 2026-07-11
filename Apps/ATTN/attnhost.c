#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

/* Host adapter for dcc I/O ports and the renamed application entry point. */
#define main attnc11_main
#include "attnc11.c"
#undef main

static struct timespec stopwatch_started;
static uint32_t stopwatch_elapsed;
static unsigned stopwatch_byte;

void outp(unsigned port, unsigned value)
{
    struct timespec now;
    int64_t seconds;

    if (port != SWPORT)
        return;
    if (value == 0) {
        timespec_get(&stopwatch_started, TIME_UTC);
        stopwatch_elapsed = 0;
        stopwatch_byte = 0;
    } else if (value == 1) {
        timespec_get(&now, TIME_UTC);
        seconds = (int64_t)now.tv_sec - stopwatch_started.tv_sec;
        if (now.tv_nsec < stopwatch_started.tv_nsec)
            seconds--;
        if (seconds < 0)
            seconds = 0;
        stopwatch_elapsed = (uint32_t)seconds;
        stopwatch_byte = 0;
    }
}

int inp(unsigned port)
{
    unsigned shift;
    int value;

    if (port != RDPORT || stopwatch_byte >= 4)
        return 0;
    shift = 24U - stopwatch_byte * 8U;
    value = (int)((stopwatch_elapsed >> shift) & 0xffU);
    stopwatch_byte++;
    return value;
}

int main(int argc, char *argv[])
{
    return attnc11_main(argc, argv);
}
