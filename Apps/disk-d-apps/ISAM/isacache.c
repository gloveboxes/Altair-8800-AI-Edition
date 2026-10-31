#include "stdlib.h"

#define CACHE_PAGES 32
#define CACHE_SETS (CACHE_PAGES / 2)
#define PAGE_SHIFT 7
#define PAGE_BYTES 128

static char *page_data;
static unsigned page_sector[CACHE_PAGES];
/* Stable table/index identity of each page; -1 is empty. */
static int page_owner[CACHE_PAGES];
static unsigned char page_bytes[CACHE_PAGES];
static unsigned char page_victim[CACHE_SETS];
/*
 * Positions are sector * 128 + offset, kept as two integers so cache hits need
 * no 32-bit arithmetic and never seek the runtime descriptor.
 */
static unsigned position_sector[11];
static unsigned char position_offset[11];
static unsigned char position_known[11];
/* Stable table/index identities preserve clean pages across durable closes. */
static int page_files[11];

static void discard_pages(int fd)
{
    int i;
    for (i = 0; i < CACHE_PAGES; ++i)
        if (fd < 0 || page_owner[i] == page_files[fd])
            page_owner[i] = -1;
}

static int start_pages(void)
{
    page_data = malloc(CACHE_PAGES * PAGE_BYTES);
    if (!page_data)
        return I_EBANK;
    discard_pages(-1);
    memset(page_victim, 0, sizeof(page_victim));
    memset(position_known, 0, sizeof(position_known));
    memset(page_files, 0xff, sizeof(page_files));
    return I_OK;
}

static void stop_pages(void)
{
    free(page_data);
    page_data = 0;
    discard_pages(-1);
}

/* Learn a descriptor's position once, when no seek has set it. */
static int page_seek(int fd, long offset);

static int learn_position(int fd)
{
    return page_seek(fd, lseek(fd, 0L, 1)) == 0;
}

static int page_seek(int fd, long offset)
{
    unsigned low, high;
    if (!page_data || fd < 3 || fd >= 11)
        return lseek(fd, offset, 0) < 0L ? -1 : 0;
    if (offset < 0L || offset >= 0x800000L)
        return -1;
    low = (unsigned)offset;
    high = (unsigned)(offset >> 16);
    position_sector[fd] = (low >> PAGE_SHIFT) | (high << (16 - PAGE_SHIFT));
    position_offset[fd] = low & (PAGE_BYTES - 1);
    position_known[fd] = 1;
    return 0;
}

static int page_read(int fd, void *buffer, unsigned int count)
{
    unsigned sector, offset, slot, set, done;
    int take, identity;
    char *output;

    if (!page_data || fd < 3 || fd >= 11 || page_files[fd] < 0)
        return read(fd, buffer, count);
    if (!position_known[fd] && !learn_position(fd))
        return -1;
    identity = page_files[fd];
    sector = position_sector[fd];
    offset = position_offset[fd];
    output = buffer;
    done = 0;
    while (count)
    {
        set = (sector + identity * 3) & (CACHE_SETS - 1);
        slot = set << 1;
        if (page_owner[slot] != identity || page_sector[slot] != sector)
        {
            ++slot;
            if (page_owner[slot] != identity || page_sector[slot] != sector)
            {
                slot = (set << 1) + page_victim[set];
                page_owner[slot] = -1;
                if (lseek(fd, (long)sector << PAGE_SHIFT, 0) < 0L)
                    return -1;
                take = read(fd, page_data + (slot << PAGE_SHIFT), PAGE_BYTES);
                if (take < 0)
                    return -1;
                page_owner[slot] = identity;
                page_sector[slot] = sector;
                page_bytes[slot] = take;
            }
        }
        page_victim[set] = 1 - (slot & 1);
        take = (int)page_bytes[slot] - (int)offset;
        if (take <= 0)
            break;
        if ((unsigned)take > count)
            take = count;
        memcpy(output + done, page_data + (slot << PAGE_SHIFT) + offset, take);
        done += take;
        count -= take;
        offset += take;
        if (offset >= PAGE_BYTES)
        {
            offset -= PAGE_BYTES;
            ++sector;
        }
    }
    position_sector[fd] = sector;
    position_offset[fd] = offset;
    return done;
}

static int page_write(int fd, const void *buffer, unsigned int count)
{
    unsigned first, last, slot, total;
    int bytes;
    if (!page_data || fd < 3 || fd >= 11)
        return write(fd, buffer, count);
    if (!position_known[fd] && !learn_position(fd))
        return -1;
    first = position_sector[fd];
    if (lseek(fd, ((long)first << PAGE_SHIFT) + position_offset[fd], 0) < 0L)
        return -1;
    if (count)
    {
        last = first + ((position_offset[fd] + count - 1) >> PAGE_SHIFT);
        for (slot = 0; slot < CACHE_PAGES; ++slot)
            if (page_owner[slot] == page_files[fd] &&
                page_sector[slot] >= first && page_sector[slot] <= last)
                page_owner[slot] = -1;
    }
    bytes = write(fd, buffer, count);
    if (bytes != count)
        discard_pages(fd);
    if (bytes > 0)
    {
        total = position_offset[fd] + bytes;
        position_sector[fd] += total >> PAGE_SHIFT;
        position_offset[fd] = total & (PAGE_BYTES - 1);
    }
    return bytes;
}

static int page_close(int fd)
{
    int rc;
    rc = close(fd);
    if (fd >= 3 && fd < 11)
    {
        if (rc != 0)
            discard_pages(fd);
        position_known[fd] = 0;
        page_files[fd] = -1;
    }
    return rc;
}
