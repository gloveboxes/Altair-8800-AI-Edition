#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BANKLIB.H"

struct BlockSpec {
    unsigned int seed;
    unsigned char label[8];
};

struct BlockInfo {
    unsigned int handle;
    unsigned int size;
    unsigned long byte_sum;
    signed char state;
    unsigned char label[8];
};

#pragma dcc bank_buffer upper 1 inout 2 128
#pragma dcc bank_buffer filerd 1 out 2 128
#pragma dcc bank_buffer blkread 3 out 4 128
#pragma dcc bank_record blknew 1 in
#pragma dcc bank_record blknew 2 out
#pragma dcc bank_record blkfree 1 inout

static unsigned char *block_data;
static unsigned int block_handle;

#define BLOCK_BYTES 4096

static void library_report_memory(void)
{
    meminfo_t info;
    meminfo(&info);
    printf("Bank 1 DCC heap: %u bytes capacity, %u committed, %u growth; stack: %u bytes\n",
           info.heap_capacity, info.heap_committed, info.heap_growth, info.stack_size);
}

unsigned int onload(void)
{
    library_report_memory();
    return 1;
}

unsigned int onunload(void)
{
    meminfo_t info;
    if (block_data)
    {
        free(block_data);
        block_data = 0;
        block_handle = 0;
    }
    meminfo(&info);
    if (info.heap_committed)
    {
        printf("Bank 1 cleanup left %u committed heap bytes.\n", info.heap_committed);
        return 0;
    }
    return 1;
}

unsigned int add16(unsigned int left, unsigned int right)
{
    return left + right;
}

unsigned int upper(unsigned char *data, unsigned int count)
{
    unsigned int i;
    if (!data || !count || count > 128)
        return 0;
    for (i = 0; i < count; ++i)
        data[i] = (unsigned char)toupper(data[i]);
    return count;
}

unsigned int filerd(unsigned char *data, unsigned int count)
{
    FILE *file;
    unsigned int result, failed;
    if (!data || !count || count > 128)
        return 0;
    file = fopen("A:AUTORUN.COM", "rb");
    if (!file)
        return 0;
    result = fread(data, 1, count, file);
    failed = ferror(file);
    if (fclose(file) || failed)
        return 0;
    return result;
}

unsigned int blknew(const struct BlockSpec *spec, struct BlockInfo *info)
{
    unsigned int i;
    if (block_data)
        return 0;
    block_data = (unsigned char *)malloc(BLOCK_BYTES);
    if (!block_data)
        return 0;
    for (i = 0; i < BLOCK_BYTES; ++i)
        block_data[i] = (unsigned char)(spec->seed + i);
    /* A runtime token is a handle, never a bank-1 pointer. */
    block_handle = *(unsigned int *)BANK_LIBRARY_CALL_TOKEN_ADDRESS;
    info->handle = block_handle;
    info->size = BLOCK_BYTES;
    /* Each full 256-byte cycle contains all byte values, regardless of seed. */
    info->byte_sum = (BLOCK_BYTES / 256UL) * 32640UL;
    info->state = 1;
    memcpy(info->label, spec->label, sizeof(info->label));
    return block_handle;
}

unsigned int blkread(unsigned int handle, unsigned int offset,
                     unsigned char *data, unsigned int count)
{
    if (!handle || handle != block_handle || !block_data ||
        !data || !count || count > 128 || offset >= BLOCK_BYTES ||
        count > BLOCK_BYTES - offset)
        return 0;
    memcpy(data, block_data + offset, count);
    return count;
}

unsigned int blkfree(struct BlockInfo *info)
{
    if (!info->handle || info->handle != block_handle || !block_data)
        return 0;
    free(block_data);
    block_data = 0;
    block_handle = 0;
    info->handle = 0;
    info->size = 0;
    info->byte_sum = 0;
    info->state = -1;
    return 1;
}
