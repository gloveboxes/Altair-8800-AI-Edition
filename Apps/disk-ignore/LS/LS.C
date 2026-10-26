/* Compact CP/M 2.2 directory listing for dcc C11. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAX_FILES = 64,
    NAME_LENGTH = 11,
    COLUMN_COUNT = 4,
    FCB_ADDRESS = 0x005c,
    DMA_ADDRESS = 0x0080,
    BDOS_SELECT_DISK = 14,
    BDOS_SEARCH_FIRST = 17,
    BDOS_SEARCH_NEXT = 18,
    BDOS_GET_CURRENT_DISK = 25,
    BDOS_GET_ALLOCATION_VECTOR = 27,
    BDOS_GET_DPB = 31,
    BDOS_GET_CURRENT_USER = 32
};

typedef struct {
    uint8_t name[NAME_LENGTH];
    uint16_t blocks;
} FileEntry;

static FileEntry sort_temp;
static FileEntry *sort_left;
static uint8_t sort_pass;
static uint8_t sort_remaining;

/* Stable largest-first sort; equal sizes retain the initial alpha order. */
static void szsort(FileEntry *base, uint8_t count)
{
    if (count < 2)
        return;
    sort_pass = count - 1;
    while (sort_pass != 0) {
        sort_left = base;
        sort_remaining = sort_pass;
        do {
            if (sort_left->blocks < sort_left[1].blocks) {
                sort_temp = *sort_left;
                *sort_left = sort_left[1];
                sort_left[1] = sort_temp;
            }
            ++sort_left;
        } while (--sort_remaining != 0);
        --sort_pass;
    }
}

static FileEntry files[MAX_FILES];
static uint8_t file_count;
static uint8_t search_fcb[36];
static bool size_order;

static void cputs(const char *text)
{
    while (*text != '\0')
        putchar((uint8_t)*text++);
}

static uint8_t count_bits(uint16_t value)
{
    uint8_t count = 0;

    while (value != 0) {
        count += value & 1;
        value >>= 1;
    }
    return count;
}

static uint16_t count_free_blocks(const uint8_t *allocation_vector,
                                  uint16_t block_count,
                                  uint8_t reserved_blocks)
{
    uint16_t free_blocks = 0;
    uint8_t bits = *allocation_vector++;
    uint8_t mask = 0x80;

    while (block_count-- != 0) {
        if (reserved_blocks != 0)
            --reserved_blocks;
        else if ((bits & mask) == 0)
            ++free_blocks;
        mask >>= 1;
        if (mask == 0 && block_count != 0) {
            bits = *allocation_vector++;
            mask = 0x80;
        }
    }
    return free_blocks;
}

static uint8_t entry_block_count(const uint8_t *entry, bool wide_blocks)
{
    uint8_t count = 0;
    uint8_t index = 16;

    while (index < 32) {
        if (entry[index] != 0 ||
            (wide_blocks && entry[index + 1] != 0))
            ++count;
        index += (uint8_t)wide_blocks + 1;
    }
    return count;
}

static bool matches_pattern(const uint8_t *name)
{
    const uint8_t *pattern = (const uint8_t *)(FCB_ADDRESS + 1);
    uint8_t index;

    if (pattern[0] == 0 || pattern[0] == ' ')
        return true;
    for (index = 0; index < NAME_LENGTH; ++index) {
        if (pattern[index] != '?' &&
            pattern[index] != (name[index] & 0x7f))
            return false;
    }
    return true;
}

static bool insert_entry(const uint8_t *entry, bool wide_blocks)
{
    FileEntry file;
    uint8_t index;
    uint8_t position = 0;
    int order = 1;

    for (index = 0; index < NAME_LENGTH; ++index)
        file.name[index] = entry[index + 1] & 0x7f;
    file.blocks = entry_block_count(entry, wide_blocks);

    while (position < file_count) {
        order = memcmp(file.name, files[position].name, NAME_LENGTH);
        if (order <= 0)
            break;
        ++position;
    }
    if (order == 0) {
        files[position].blocks += file.blocks;
        return true;
    }
    if (file_count == MAX_FILES)
        return false;

    memmove(&files[position + 1], &files[position],
            (file_count - position) * sizeof(files[0]));
    files[position] = file;
    ++file_count;
    return true;
}

static bool read_directory(uint8_t user, bool wide_blocks,
                           uint16_t *used_entries)
{
    uint8_t *fcb = search_fcb;
    uint8_t *dma = (uint8_t *)DMA_ADDRESS;
    uint8_t *entry;
    uint8_t index;
    int result;

    for (index = 0; index < 36; ++index)
        fcb[index] = 0;
    /* Raw search returns every directory slot, including later extents. */
    fcb[0] = '?';

    result = bdos(BDOS_SEARCH_FIRST, (int)fcb) & 0xff;
    while (result != 0xff) {
        entry = dma + ((result & 3) << 5);
        if (entry[0] != 0xe5) {
            ++*used_entries;
            if (entry[0] == user && matches_pattern(entry + 1) &&
                !insert_entry(entry, wide_blocks))
                return false;
        }
        result = bdos(BDOS_SEARCH_NEXT, (int)fcb) & 0xff;
    }
    return true;
}

static void put_number(uint16_t value)
{
    static const uint16_t places[] = { 10000, 1000, 100, 10, 1 };
    bool started = false;
    uint8_t digit;
    uint8_t index;

    for (index = 0; index < 5; ++index) {
        digit = 0;
        while (value >= places[index]) {
            value -= places[index];
            ++digit;
        }
        if (digit != 0 || started || index == 4) {
            putchar('0' + digit);
            started = true;
        }
    }
}

static void put_size(uint16_t value)
{
    if (value < 1000)
        putchar(' ');
    if (value < 100)
        putchar(' ');
    if (value < 10)
        putchar(' ');
    put_number(value);
    putchar('K');
}

static void put_file(const FileEntry *file, uint8_t block_shift)
{
    uint8_t index;

    for (index = 0; index < NAME_LENGTH; ++index)
        putchar(file->name[index]);
    putchar(' ');
    put_size(file->blocks << block_shift);
}

static void put_listing(uint8_t block_shift)
{
    uint8_t row_count = (file_count + COLUMN_COUNT - 1) >> 2;
    uint8_t row;
    uint8_t column;
    uint8_t offset;

    cputs("\x1b[94mName    Ext Bytes   Name    Ext Bytes   Name    Ext Bytes   Name    Ext Bytes\x1b[0m\r\n");
    for (row = 0; row < row_count; ++row) {
        offset = row;
        for (column = 0; column < COLUMN_COUNT; ++column) {
            if (offset < file_count) {
                put_file(&files[offset], block_shift);
                if (offset + row_count < file_count)
                    cputs(" | ");
            }
            offset += row_count;
        }
        cputs("\r\n");
    }
}

int main(void)
{
    uint8_t *options = (uint8_t *)FCB_ADDRESS;
    const uint8_t *dpb;
    const uint8_t *allocation_vector;
    uint16_t disk_blocks;
    uint16_t directory_entries;
    uint16_t used_entries = 0;
    uint16_t free_blocks;
    uint16_t total_blocks;
    uint16_t total_k;
    uint16_t free_k;
    uint8_t reserved_blocks;
    uint8_t block_shift;
    uint8_t drive;
    uint8_t original_drive;
    uint8_t requested_drive;
    uint8_t user;

    if (options[1] == '-' && options[2] == 'S') {
        size_order = true;
        memmove(options, options + 16, 12);
    } else if (options[17] == '-' && options[18] == 'S')
        size_order = true;
    original_drive = bdos(BDOS_GET_CURRENT_DISK, 0) & 0xff;
    requested_drive = *(uint8_t *)FCB_ADDRESS;
    if (requested_drive != 0)
        bdos(BDOS_SELECT_DISK, requested_drive - 1);

    dpb = (const uint8_t *)bdoshl(BDOS_GET_DPB, 0);
    disk_blocks = *(const uint16_t *)(dpb + 5) + 1;
    directory_entries = *(const uint16_t *)(dpb + 7) + 1;
    reserved_blocks = count_bits((uint16_t)dpb[9] << 8 | dpb[10]);
    block_shift = dpb[2] - 3;
    drive = bdos(BDOS_GET_CURRENT_DISK, 0) & 0xff;
    user = bdos(BDOS_GET_CURRENT_USER, 0xff) & 0xff;

    if (!read_directory(user, disk_blocks > 256, &used_entries)) {
        bdos(BDOS_SELECT_DISK, original_drive);
        cputs("LS: too many files\r\n");
        return 1;
    }
    allocation_vector =
        (const uint8_t *)bdoshl(BDOS_GET_ALLOCATION_VECTOR, 0);
    free_blocks = count_free_blocks(allocation_vector, disk_blocks,
                                    reserved_blocks);
    total_blocks = disk_blocks - reserved_blocks;
    total_k = total_blocks << block_shift;
    free_k = free_blocks << block_shift;
    bdos(BDOS_SELECT_DISK, original_drive);

    if (size_order)
        szsort(files, file_count);
    put_listing(block_shift);
    put_number(file_count);
    cputs(" File(s), occupying ");
    put_number(total_k - free_k);
    putchar('K');
    cputs(" of ");
    put_number(total_k);
    cputs("K total capacity\r\n");
    put_number(directory_entries - used_entries);
    cputs(" directory entries and ");
    put_number(free_k);
    cputs("K bytes remain on ");
    putchar('A' + drive);
    cputs(":\r\n");
    return 0;
}