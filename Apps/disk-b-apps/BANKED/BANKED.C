#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BANKMEM.H"

#define BANK_BYTES 5632U
#define CHUNK_BYTES 256U
#define RUN_SECONDS 43200UL
#define TIMER_PORT 0x17
#define STOPWATCH_START 0x40
#define STOPWATCH_SECONDS 0x50
#define PATTERN_COUNT 24U

/* Keep the working set small: the bank is tested through a 256-byte buffer. */
static unsigned char buffer[CHUNK_BYTES];
static unsigned long elapsed_cache;
static unsigned long last_ui_seconds;
static unsigned long completed_iterations;
static unsigned long total_access_calls;
static unsigned long read_access_calls;
static unsigned long write_access_calls;
static unsigned long accesses_at_start;

static int bank_write_access(bank_memory_handle_t handle, unsigned int offset,
                             const void *data, unsigned int bytes)
{
    if (!bank_memory_write(handle, offset, data, bytes))
        return 0;
    if (write_access_calls != 0xFFFFFFFFUL)
        ++write_access_calls;
    if (total_access_calls != 0xFFFFFFFFUL)
        ++total_access_calls;
    return 1;
}

static int bank_read_access(bank_memory_handle_t handle, unsigned int offset,
                            void *data, unsigned int bytes)
{
    if (!bank_memory_read(handle, offset, data, bytes))
        return 0;
    if (read_access_calls != 0xFFFFFFFFUL)
        ++read_access_calls;
    if (total_access_calls != 0xFFFFFFFFUL)
        ++total_access_calls;
    return 1;
}

static unsigned long stopwatch_seconds(void)
{
    unsigned long seconds;

    outp(TIMER_PORT, STOPWATCH_SECONDS);
    seconds = (unsigned long)inp(TIMER_PORT);
    seconds |= (unsigned long)inp(TIMER_PORT) << 8;
    seconds |= (unsigned long)inp(TIMER_PORT) << 16;
    seconds |= (unsigned long)inp(TIMER_PORT) << 24;
    return seconds;
}

static int update_ui(void)
{
    unsigned long remaining, average_ops;

    elapsed_cache = stopwatch_seconds();
    if (elapsed_cache == 0xFFFFFFFFUL)
        return 0;
    if (elapsed_cache - last_ui_seconds >= 10UL ||
        (elapsed_cache >= RUN_SECONDS && last_ui_seconds < RUN_SECONDS))
    {
        remaining = elapsed_cache >= RUN_SECONDS ? 0 : RUN_SECONDS - elapsed_cache;
        average_ops = elapsed_cache ?
            (total_access_calls - accesses_at_start) / elapsed_cache : 0;
        printf("Elapsed: %lu s; remaining: %lu s; iterations: %lu; "
               "bank data access calls total/read/write: %lu/%lu/%lu; "
               "avg data ops/s: %lu\n",
               elapsed_cache, remaining, completed_iterations, total_access_calls,
               read_access_calls, write_access_calls, average_ops);
        last_ui_seconds = elapsed_cache;
    }
    return 1;
}

static unsigned char pattern_byte(unsigned int offset, unsigned int pattern)
{
    unsigned char low, value;
    unsigned char bit;

    low = (unsigned char)offset;
    if (pattern == 0) return 0x00;
    if (pattern == 1) return 0xFF;
    if (pattern == 2) return 0x55;
    if (pattern == 3) return 0xAA;
    if (pattern == 4) return low;
    if (pattern == 5) return (unsigned char)~low;
    value = (unsigned char)(low ^ (unsigned char)(offset >> 8));
    if (pattern == 6) return value;
    if (pattern == 7) return (unsigned char)~value;

    bit = (unsigned char)(1U << ((pattern - 8U) >> 1));
    if ((pattern & 1U) == 0)
        return bit;
    return (unsigned char)~bit;
}

/* Return 1 when complete, 0 on ESC, and -1 on a memory/API failure. */
static int test_pattern(bank_memory_handle_t whole_bank, unsigned int pattern)
{
    unsigned int offset, i, phase, chunks_since_update;
    int key;

    chunks_since_update = 0;
    /* Retain the entire bank before verification so aliased pages cannot
     * hide behind a successful immediate write/read of the same address. */
    for (phase = 0; phase < 2; ++phase)
    {
        for (offset = 0; offset < BANK_BYTES; offset += CHUNK_BYTES)
        {
            for (i = 0; i < CHUNK_BYTES; ++i)
                buffer[i] = phase == 0 ? pattern_byte(offset + i, pattern) :
                    (unsigned char)~pattern_byte(offset + i, pattern);
            if (phase == 0 && !bank_write_access(whole_bank, offset, buffer, CHUNK_BYTES))
            {
                printf("FAIL: write pattern %u at %u\n", pattern, offset);
                return -1;
            }
            /* Every byte starts wrong, so a skipped or partial read must fail. */
            if (phase == 1 && !bank_read_access(whole_bank, offset, buffer, CHUNK_BYTES))
            {
                printf("FAIL: read pattern %u at %u\n", pattern, offset);
                return -1;
            }
            if (phase == 1)
            {
                for (i = 0; i < CHUNK_BYTES; ++i)
                    if (buffer[i] != pattern_byte(offset + i, pattern))
                    {
                        printf("FAIL: pattern %u mismatch at %u, got %02x\n",
                               pattern, offset + i, buffer[i]);
                        return -1;
                    }
            }

            if (++chunks_since_update == 8)
            {
                chunks_since_update = 0;
                if (!update_ui())
                {
                    puts("FAIL: timer port 0x17 stopwatch read failed.");
                    return -1;
                }
            }
            if (kbhit())
            {
                key = getch();
                if (key == 27)
                    return 0;
            }
        }
    }
    if (chunks_since_update && !update_ui())
    {
        puts("FAIL: timer port 0x17 stopwatch read failed.");
        return -1;
    }
    return 1;
}

static int allocator_checks(void)
{
    bank_memory_handle_t a, b, c, d, again_b, again_d;
    unsigned char value;

    a = bank_memory_allocate(1);
    b = bank_memory_allocate(257);
    c = bank_memory_allocate(1024);
    d = bank_memory_allocate(2048);
    if (!a || !b || !c || !d || bank_memory_size(a) != 256 ||
        bank_memory_size(b) != 512 || bank_memory_size(c) != 1024 || bank_memory_size(d) != 2048)
        return 0;

    /* Full bursts take the direct public-BIOS path. Its base-range checks
     * must reject low memory, resident-page overlap and 16-bit wrapping. */
    if (bank_memory_read(a, 0, (void *)0x80, 16) ||
        bank_memory_write(a, 0, (void *)0x80, 16) ||
        bank_memory_read(a, 0, (void *)0xFEF0, 32) ||
        bank_memory_write(a, 0, (void *)0xFEF0, 32) ||
        bank_memory_read(a, 0, (void *)0xFFF0, 32) ||
        bank_memory_write(a, 0, (void *)0xFFF0, 32) ||
        !bank_memory_read(a, 256, (void *)0xFFFF, 0) ||
        !bank_memory_write(a, 256, (void *)0xFFFF, 0))
        return 0;

    value = 0xA5;
    if (!bank_write_access(a, 0, &value, 1) || !bank_read_access(a, 0, &buffer[0], 1) ||
        buffer[0] != value)
        return 0;
    value = 0x5A;
    if (!bank_write_access(b, 511, &value, 1) || !bank_read_access(b, 511, &buffer[0], 1) ||
        buffer[0] != value)
        return 0;
    value = 0xC3;
    if (!bank_write_access(d, 2047, &value, 1) || !bank_read_access(d, 2047, &buffer[0], 1) ||
        buffer[0] != value)
        return 0;

    if (!bank_memory_free(b) || !bank_memory_free(d))
        return 0;
    again_b = bank_memory_allocate(257);
    again_d = bank_memory_allocate(2048);
    if (again_b != b || again_d != d || !bank_memory_free(again_b) ||
        !bank_memory_free(again_d) || !bank_memory_free(a) || !bank_memory_free(c) || bank_memory_available() != BANK_BYTES)
        return 0;
    return 1;
}

int main(void)
{
    bank_memory_handle_t whole_bank;
    unsigned int pattern;
    unsigned long cycle;
    int result, duration_done;

    if (!bank_memory_initialize())
    {
        puts("BANKED: banked SRAM unavailable; check A16 wiring and firmware.");
        return 1;
    }
    puts("BANKED: resetting shared bank-1 heap (all old handles invalid).");
    if (!bank_memory_reset())
        return 1;
    printf("Usable extended RAM: %u bytes; base test buffer: %u bytes\n",
           bank_memory_available(), CHUNK_BYTES);
    puts("Test plan:");
    puts("  Allocation rounding, fragmentation, free/reuse and exhaustion.");
    puts("  One-byte transfers at allocation ends, bounds rejection and FEFFh.");
    puts("  Every usable bank byte: full-bank write, then independent read/compare.");
    puts("  00/FF/55/AA; byte-address and page-address patterns and inverses.");
    puts("  All eight walking-one and walking-zero bit patterns (24 total).");
    puts("  Read buffers are poisoned before verification to detect missed reads.");
#ifdef BANKED_ONE_CYCLE
    puts("  Finite emulator build: one complete 24-pattern iteration.");
#else
    puts("  Repeat for 43200 seconds; status every 10 seconds; ESC stops early.");
#endif

    if (!allocator_checks())
    {
        puts("FAIL: allocator fragmentation, reuse, or boundary checks.");
        return 1;
    }

    whole_bank = bank_memory_allocate(BANK_BYTES);
    if (!whole_bank)
    {
        puts("FAIL: cannot allocate all usable banked SRAM.");
        return 1;
    }
    if (bank_memory_allocate(1) || bank_memory_allocate(BANK_BYTES + 1U))
    {
        puts("FAIL: full allocation did not exhaust the heap.");
        return 1;
    }
    if (bank_write_access(whole_bank, BANK_BYTES, buffer, 1))
    {
        puts("FAIL: out-of-range write was accepted.");
        return 1;
    }
    if (!bank_write_access(whole_bank, BANK_BYTES - 1U, "Z", 1) ||
        !bank_read_access(whole_bank, BANK_BYTES - 1U, buffer, 1) || buffer[0] != 'Z')
    {
        puts("FAIL: FEFFh boundary byte did not verify.");
        return 1;
    }

    accesses_at_start = total_access_calls;
    /* A previous client may have left parameters or an error pending.
     * IN aborts parameter collection; OUT 0 then resets the parser. */
    inp(TIMER_PORT);
    outp(TIMER_PORT, 0);
    outp(TIMER_PORT, STOPWATCH_START);
    puts("12-hour run started; countdown uses timer port 0x17. ESC stops early.");
    printf("Elapsed: 0 s; remaining: 43200 s; iterations: 0; "
           "bank data access calls total/read/write: %lu/%lu/%lu; avg data ops/s: 0\n",
           total_access_calls, read_access_calls, write_access_calls);
    puts("Each cycle writes and verifies all 5632 shared-pool bytes with 24 data patterns.");
    puts("Patterns: 00/FF/55/AA, address and inverse, walking 1s and 0s.");

    cycle = 0;
    elapsed_cache = 0;
    last_ui_seconds = 0;
    completed_iterations = 0;
    duration_done = 0;
    for (;;)
    {
        ++cycle;
        for (pattern = 0; pattern < PATTERN_COUNT; ++pattern)
        {
            result = test_pattern(whole_bank, pattern);
            if (result < 0)
            {
                bank_memory_free(whole_bank);
                return 1;
            }
            if (result == 0)
            {
                bank_memory_free(whole_bank);
                printf("Stopped by ESC; iterations completed: %lu\n",
                       completed_iterations);
                printf("Bank data access calls total/read/write: %lu/%lu/%lu\n",
                       total_access_calls, read_access_calls, write_access_calls);
                return 0;
            }

            if (pattern + 1U == PATTERN_COUNT)
                ++completed_iterations;
            if (elapsed_cache >= RUN_SECONDS)
            {
                duration_done = 1;
                break;
            }
        }

        if (duration_done)
        {
            if (completed_iterations == cycle)
                printf("PASS: cycle %lu completed at the 12-hour limit.\n", cycle);
            else
                printf("12-hour limit reached after pattern %u of cycle %lu.\n",
                       pattern + 1U, cycle);
            break;
        }
#ifdef BANKED_ONE_CYCLE
        printf("PASS: cycle %lu; all %u usable bytes verified with %u patterns.\n",
               cycle, BANK_BYTES, PATTERN_COUNT);
        break;
#endif
    }

    if (!bank_memory_free(whole_bank) || bank_memory_available() != BANK_BYTES)
    {
        puts("FAIL: heap did not return to its initial free size.");
        return 1;
    }
    printf("Free banked memory: %u bytes\n", bank_memory_available());
    printf("Iterations completed: %lu\n", completed_iterations);
    printf("Bank data access calls total/read/write: %lu/%lu/%lu\n",
           total_access_calls, read_access_calls, write_access_calls);
    if (duration_done)
        puts("PASS: 12-hour banked memory endurance run completed.");
    else
        puts("PASS: finite emulator banked-memory cycle completed.");
    return 0;
}
