/* Benchmark the BIOS ABI 2.3 bank-1 extent allocator. */
#include <stdio.h>
#include <string.h>
#include "BANKMEM.H"
#include "BIOSBANK.H"

#ifndef ITERATIONS
#define ITERATIONS 512U
#endif
#define TIMER_PORT 0x17

static void start(void)
{
    inp(TIMER_PORT);
    outp(TIMER_PORT, 0);
    outp(TIMER_PORT, 0x40);
}

static unsigned long elapsed(void)
{
    unsigned long value;
    outp(TIMER_PORT, 0x60);
    value = (unsigned long)inp(TIMER_PORT);
    value |= (unsigned long)inp(TIMER_PORT) << 8;
    value |= (unsigned long)inp(TIMER_PORT) << 16;
    value |= (unsigned long)inp(TIMER_PORT) << 24;
    return value;
}

static int measure(unsigned int bytes, unsigned long *ms)
{
    unsigned int i, free_before = bavail();
    bhandle handle;
    int success = 1;
    /* Warm up and verify size/cleanup before starting the stopwatch. */
    handle = bmalloc(bytes);
    if (!handle || bsize(handle) != bytes ||
        !bfree(handle) || bavail() != free_before)
        return 0;
    start();
    for (i = 0; i < ITERATIONS; ++i)
    {
        handle = bmalloc(bytes);
        if (!handle || !bfree(handle))
        {
            success = 0;
            break;
        }
    }
    *ms = elapsed();
    return success && *ms != 0xFFFFFFFFUL && bavail() == free_before;
}

static void cell(unsigned long ms)
{
    if (!ms)
        printf("<1 ms");
    else
        printf("%lu ms; %lu us/pair", ms, ms * 1000UL / ITERATIONS);
}

static int cases(void)
{
    unsigned int i;
    unsigned int sizes[3];
    unsigned long ms;
    sizes[0] = 256;
    sizes[1] = 4096;
    sizes[2] = 32768U;
    for (i = 0; i < 3; ++i)
    {
        if (!measure(sizes[i], &ms))
            return 0;
        printf("%u bytes | Bank 1: ", sizes[i]);
        cell(ms);
        putchar('\n');
    }
    return 1;
}

int main(void)
{
    bhandle prefix;
    bhandle holes[32];
    unsigned int i;
    unsigned int free_before;
    int success;
    puts("ALLOCPRF: bank-1 extent allocator performance.");
    printf("%u allocate/free pairs per measurement; bank-1 extent allocation and free.\n", ITERATIONS);
    puts("Times include allocation, free, loop and result checks; no payload copies.");
    puts("Warm-up, size/free-space checks and printing are outside timing.");
    if (!binit())
    {
        puts("FAIL: matching BIOS ABI 2.3 and allocator firmware required.");
        return 1;
    }
    free_before = bavail();
    if (free_before != BMBIOS_HEAP_BYTES)
    {
        puts("FAIL: benchmark requires an empty heap; existing allocations preserved.");
        return 1;
    }
    puts("Case: empty heap.");
    success = cases();
    prefix = 0;
    if (success)
    {
        prefix = bmalloc(16384U);
        if (!prefix)
            success = 0;
        else
        {
            puts("Case: first 16 KiB occupied (longer first-fit scan).");
            success = cases();
        }
    }
    if (!bfree(prefix) || bavail() != free_before)
        success = 0;
    memset(holes, 0, sizeof(holes));
    if (success)
    {
        for (i = 0; i < 32; ++i)
        {
            holes[i] = bmalloc(256);
            if (!holes[i])
            {
                success = 0;
                break;
            }
        }
        for (i = 0; i < 32; i += 2)
        {
            if (!bfree(holes[i]))
                success = 0;
            holes[i] = 0;
        }
        if (success)
        {
            puts("Case: 16 small holes separated by live allocations.");
            success = cases();
        }
    }
    for (i = 0; i < 32; ++i)
        if (!bfree(holes[i]))
            success = 0;
    if (bavail() != free_before)
        success = 0;
    if (!success)
    {
        puts("FAIL: allocation, cleanup, or millisecond stopwatch.");
        return 1;
    }
    puts("PASS: allocator benchmark complete; heap restored.");
    return 0;
}
