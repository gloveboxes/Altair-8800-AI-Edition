#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BANKMEM.H"
#include "BIOSBANK.H"

#define TEST_BYTES 32768U
#define LOCAL_BYTES 16384U
#define TIMER_PORT 0x17
#define STOPWATCH_START 0x40
#define STOPWATCH_MS 0x60

/* This benchmark deliberately spends 32 KiB of TPA to prepare every source
 * byte before timing and to retain every read byte for later verification. */
static unsigned char buffer[TEST_BYTES];

typedef struct {
    unsigned long write_ms;
    unsigned long read_ms;
} timings;

static void explain_bank_failure(void)
{
    unsigned char header[BMBIOS_BURST_BYTES];
    unsigned int version;

    if (memcmp((void *)BMBIOS_SIGNATURE, "BMX2", 4) ||
        *(unsigned char *)BMBIOS_VERSION != BMBIOS_ABI_MAJOR)
    {
        puts("BIOS bank extension descriptor missing or incompatible.");
        return;
    }
    version = bmstat();
    if ((version >> 8) != BMBIOS_ABI_MAJOR)
    {
        printf("BIOS INFO reports banked SRAM unavailable (ABI %04x).\n", version);
        return;
    }
    memset(header, 0xEE, sizeof(header));
    if (!bmread(0, header, sizeof(header)))
    {
        puts("BIOS INFO succeeded, but the banked heap-header read failed.");
        return;
    }
    printf("Heap signature: %02x %02x %02x %02x; expected 42 4b 4d 33 (BKM3).\n",
           header[0], header[1], header[2], header[3]);
    if (memcmp(header + 4, "BEX1", 4))
        puts("Bank-1 allocator missing; flash matching Phase 10 firmware, then restart.");
    puts("Check that both banks have the same resident BIOS page.");
}

static unsigned char expected_byte(unsigned int offset)
{
    return (unsigned char)((offset & 255U) ^ (offset >> 8) ^ 0x5AU);
}

static void start_watch(void)
{
    /* Abort a previous client's incomplete command, then reset the parser. */
    inp(TIMER_PORT);
    outp(TIMER_PORT, 0);
    outp(TIMER_PORT, STOPWATCH_START);
}

static unsigned long elapsed_ms(void)
{
    unsigned long value;

    outp(TIMER_PORT, STOPWATCH_MS);
    value = (unsigned long)inp(TIMER_PORT);
    value |= (unsigned long)inp(TIMER_PORT) << 8;
    value |= (unsigned long)inp(TIMER_PORT) << 16;
    value |= (unsigned long)inp(TIMER_PORT) << 24;
    return value;
}

static void print_cell(unsigned long milliseconds)
{
    unsigned long rate_tenths;

    if (!milliseconds)
    {
        printf("<1 ms (below timer resolution)");
        return;
    }
    /* 32 KiB * 1000 ms/s * 10 tenths; no floating point or overflow. */
    rate_tenths = 320000UL / milliseconds;
    printf("%lu ms; %lu.%lu KiB/s", milliseconds,
           rate_tenths / 10UL, rate_tenths % 10UL);
}

static int benchmark(bhandle data, unsigned int chunk_bytes, timings *result)
{
    unsigned int offset, i;
    unsigned long write_ms, read_ms;

    for (i = 0; i < TEST_BYTES; ++i)
        buffer[i] = expected_byte(i);

    start_watch();
    for (offset = 0; offset < TEST_BYTES; offset += chunk_bytes)
        if (!bwrite(data, offset, buffer + offset, chunk_bytes))
        {
            puts("FAIL: banked write rejected.");
            return 0;
        }
    write_ms = elapsed_ms();
    if (write_ms == 0xFFFFFFFFUL)
    {
        puts("FAIL: millisecond stopwatch read after write.");
        return 0;
    }

    /* Poison every byte before timing, so skipped/partial reads cannot pass. */
    for (i = 0; i < TEST_BYTES; ++i)
        buffer[i] = (unsigned char)~expected_byte(i);

    start_watch();
    for (offset = 0; offset < TEST_BYTES; offset += chunk_bytes)
        if (!bread(data, offset, buffer + offset, chunk_bytes))
        {
            puts("FAIL: banked read rejected.");
            return 0;
        }
    read_ms = elapsed_ms();
    if (read_ms == 0xFFFFFFFFUL)
    {
        puts("FAIL: millisecond stopwatch read after read.");
        return 0;
    }

    /* Validation and reporting are entirely outside both timed intervals. */
    for (i = 0; i < TEST_BYTES; ++i)
        if (buffer[i] != expected_byte(i))
        {
            printf("FAIL: verification at offset %u, expected %02x, got %02x\n",
                   i, expected_byte(i), buffer[i]);
            return 0;
        }

    result->write_ms = write_ms;
    result->read_ms = read_ms;
    return 1;
}

static int benchmark_local(unsigned int chunk_bytes, timings *result)
{
    unsigned char *store = buffer;
    unsigned char *io = buffer + LOCAL_BYTES;
    unsigned int i, offset, pass, local_chunk;

    /* Reuse the 32 KiB buffer as two independent 16 KiB regions. Each
     * timed direction copies the region twice, moving 32 KiB in total. */
    local_chunk = chunk_bytes;
    if (local_chunk > LOCAL_BYTES)
        local_chunk = LOCAL_BYTES;
    for (i = 0; i < LOCAL_BYTES; ++i)
    {
        io[i] = expected_byte(i);
        store[i] = (unsigned char)~expected_byte(i);
    }
    start_watch();
    for (pass = 0; pass < 2; ++pass)
        for (offset = 0; offset < LOCAL_BYTES; offset += local_chunk)
            memcpy(store + offset, io + offset, local_chunk);
    result->write_ms = elapsed_ms();
    if (result->write_ms == 0xFFFFFFFFUL)
    {
        puts("FAIL: local RAM write stopwatch read.");
        return 0;
    }
    for (i = 0; i < LOCAL_BYTES; ++i)
        if (store[i] != expected_byte(i))
        {
            printf("FAIL: local RAM write verification at offset %u\n", i);
            return 0;
        }

    for (i = 0; i < LOCAL_BYTES; ++i)
        io[i] = (unsigned char)~expected_byte(i);
    start_watch();
    for (pass = 0; pass < 2; ++pass)
        for (offset = 0; offset < LOCAL_BYTES; offset += local_chunk)
            memcpy(io + offset, store + offset, local_chunk);
    result->read_ms = elapsed_ms();
    if (result->read_ms == 0xFFFFFFFFUL)
    {
        puts("FAIL: local RAM read stopwatch read.");
        return 0;
    }
    for (i = 0; i < LOCAL_BYTES; ++i)
        if (io[i] != expected_byte(i))
        {
            printf("FAIL: local RAM read verification at offset %u\n", i);
            return 0;
        }
    return 1;
}

static void print_comparison(unsigned int chunk_bytes,
                             const timings *local, const timings *banked)
{
    if (chunk_bytes == TEST_BYTES)
        puts("Bulk: base RAM 2 x 16384-byte copies; banked RAM 1 x 32768-byte call.");
    else
        puts("256-byte chunks: 128 calls per direction for both memory types.");
    puts("32768 bytes per timed direction; local copies use runtime memcpy.");
    puts("Direction | Z80 base RAM | Banked RAM");
    printf("Write     | ");
    print_cell(local->write_ms);
    printf(" | ");
    print_cell(banked->write_ms);
    putchar('\n');
    printf("Read      | ");
    print_cell(local->read_ms);
    printf(" | ");
    print_cell(banked->read_ms);
    putchar('\n');
    puts("PASS: banked payload and both local RAM copy directions verified after timing.");
}

int main(void)
{
    bhandle data;
    unsigned int free_before;
    int success;
    timings local, banked;

    puts("MEMPERF: Z80 base RAM versus banked SRAM, 32 KiB per timed direction.");
    puts("BANKMEM API: one 32 KiB call, then 128 x 256-byte calls.");
    puts("32 KiB base buffer; preparation, poisoning and verification excluded.");
    puts("Base RAM uses two 16 KiB buffer regions; neither code nor CP/M is overwritten.");
    puts("Timing: port 0x17, stopwatch 0, whole milliseconds; no output while timing.");
    if (!binit())
    {
        puts("FAIL: banked SRAM unavailable; check BIOS, firmware and A16 wiring.");
        explain_bank_failure();
        return 1;
    }
    free_before = bavail();
    data = bmalloc(TEST_BYTES);
    if (!data)
    {
        printf("FAIL: need a contiguous 32768-byte allocation; %u bytes free.\n",
               free_before);
        return 1;
    }

    success = benchmark(data, TEST_BYTES, &banked);
    if (success)
        success = benchmark_local(TEST_BYTES, &local);
    if (success)
    {
        print_comparison(TEST_BYTES, &local, &banked);
        success = benchmark(data, 256U, &banked);
    }
    if (success)
        success = benchmark_local(256U, &local);
    if (success)
        print_comparison(256U, &local, &banked);
    if (!bfree(data) || bavail() != free_before)
    {
        puts("FAIL: allocation cleanup or heap free-size check.");
        return 1;
    }
    if (!success)
        return 1;
    printf("PASS: MEMPERF complete; %u bytes free.\n", bavail());
    return 0;
}
