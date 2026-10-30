#include "stdlib.h"

#define CACHE_PAGES 32
#define CACHE_SETS (CACHE_PAGES / 2)

static char *page_data;
static struct {
    long offset;
    int fd;
    int bytes;
} pages[CACHE_PAGES];
static unsigned char page_victim[CACHE_SETS];
/* Cache hits move logical positions without seeking the runtime descriptor. */
static long file_position[11];
static unsigned char position_known[11];
/* Stable table/index identities preserve clean pages across durable closes. */
static int page_files[11];

static void discard_pages(int fd)
{
    int i;
    for (i = 0; i < CACHE_PAGES; ++i)
        if (fd < 0 || pages[i].fd == page_files[fd])
            pages[i].fd = -1;
}

static int start_pages(void)
{
    page_data = malloc(CACHE_PAGES * 128);
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

static long page_position(int fd)
{
    long position;
    if (!position_known[fd])
    {
        position = lseek(fd, 0L, 1);
        if (position < 0L)
            return -1L;
        file_position[fd] = position;
        position_known[fd] = 1;
    }
    return file_position[fd];
}

static long page_seek(int fd, long offset, int origin)
{
    long position;
    if (!page_data || fd < 3 || fd >= 11 || origin == 2 ||
        origin < 0 || origin > 2)
    {
        position = lseek(fd, offset, origin);
        if (fd >= 3 && fd < 11 && position >= 0L)
        {
            file_position[fd] = position;
            position_known[fd] = 1;
        }
        return position;
    }
    position = offset;
    if (origin == 1)
    {
        position = page_position(fd);
        if (position < 0L)
            return position;
        position += offset;
    }
    if (position < 0L)
        return lseek(fd, -1L, 0);
    file_position[fd] = position;
    position_known[fd] = 1;
    return position;
}

static int page_read(int fd, void *buffer, unsigned int count)
{
    long position;
    long sector;
    char *output;
    int set;
    int slot;
    int offset;
    int take;
    int done;
    int bytes, identity;

    if (!page_data || fd < 3 || fd >= 11 || page_files[fd] < 0)
        return read(fd, buffer, count);
    identity = page_files[fd];
    position = page_position(fd);
    if (position < 0L)
        return -1;
    output = buffer;
    done = 0;
    while (count)
    {
        sector = position & ~127L;
        offset = (unsigned int)position & 127;
        set = ((unsigned int)(position >> 7) + identity * 3) & (CACHE_SETS - 1);
        slot = set * 2;
        if (pages[slot].fd != identity || pages[slot].offset != sector)
        {
            ++slot;
            if (pages[slot].fd != identity || pages[slot].offset != sector)
            {
                slot = set * 2 + page_victim[set];
                pages[slot].fd = -1;
                if (lseek(fd, sector, 0) < 0L)
                    return -1;
                bytes = read(fd, page_data + slot * 128, 128);
                if (bytes < 0)
                    return -1;
                pages[slot].fd = identity;
                pages[slot].offset = sector;
                pages[slot].bytes = bytes;
            }
        }
        page_victim[set] = 1 - (slot & 1);
        take = pages[slot].bytes - offset;
        if (take <= 0)
            break;
        if (take > count)
            take = count;
        memcpy(output + done, page_data + slot * 128 + offset, take);
        done += take;
        position += take;
        count -= take;
    }
    file_position[fd] = position;
    return done;
}

static int page_write(int fd, const void *buffer, unsigned int count)
{
    long position;
    long end;
    int bytes, slot;
    if (!page_data || fd < 3 || fd >= 11)
        return write(fd, buffer, count);
    position = page_position(fd);
    if (position < 0L || lseek(fd, position, 0) < 0L)
        return -1;
    end = position + count;
    for (slot = 0; slot < CACHE_PAGES; ++slot)
        if (pages[slot].fd == page_files[fd] && pages[slot].offset < end &&
            pages[slot].offset + 128L > position)
            pages[slot].fd = -1;
    bytes = write(fd, buffer, count);
    if (bytes != count)
        discard_pages(fd);
    if (bytes > 0)
        file_position[fd] += bytes;
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
