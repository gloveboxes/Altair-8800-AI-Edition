#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "isamdb.h"

#ifdef ISAM_BANK_LIBRARY
#define engine_close page_close
#define engine_read page_read
#define engine_write page_write
#define engine_seek page_seek
#else
#define engine_close close
#define engine_read read
#define engine_write write
#define engine_seek lseek
#endif

#define PRIMARY_INDEX_HEADER_SIZE 24
#define PRIMARY_INDEX_MAGIC_0 'D'
#define PRIMARY_INDEX_MAGIC_1 'X'
#define PRIMARY_INDEX_MAGIC_2 'I'
#define PRIMARY_INDEX_MAGIC_3 '4'
#define NO_FREE_SLOT 0xffff
#define DELETED_INDEX_SLOT 0xffff

struct persistent_metadata
{
    int index_count;
    int active_count;
    int high_water_slot;
    int free_head;
    int generation;
};

struct key_change
{
    struct persistent_metadata metadata;
    int position;
    int old_position;
    int found;
    char key[I_MXKEYLN];
};

static char record_workspace[I_RECSZ];
static char key_record_workspace[I_RECSZ];
/* Mutations are serialized; keep plans off the small bank-1 stack. */
static struct key_change key_changes[I_MXKEY];
#define FILE_CACHE_SIZE 6
#ifdef ISAM_BANK_LIBRARY
static struct persistent_metadata header_cache[FILE_CACHE_SIZE];
static unsigned char header_known[FILE_CACHE_SIZE];
#endif
static struct {
    int fd;
    int identity;
    int references;
    unsigned int age;
} file_cache[FILE_CACHE_SIZE];
static int file_slots[I_MXTBL * (I_MXKEY + 1)];
static int fd_slots[11];
static unsigned int file_clock;
static int files_initialized;
static char cursor_key[I_MXKEYLN + 2];
/* Reset at each scalar/batch entry; engine operations are serialized. */
static int lookup_position;
static int defer_table_file_invalidation;
static int cached_close_error;

static int checked_table(int table)
{
    return table >= 0 && table < g_cfg.ntbls && table < I_MXTBL ?
        table : I_ENTBL;
}

static int find_table_index(const char *tblnam)
{
    int i;
    const char *left;
    const char *right;

    for (i = 0; i < g_cfg.ntbls && i < I_MXTBL; i++)
    {
        left = tblnam;
        right = g_cfg.tbls[i].name;
        while (*left && *right && *left == *right)
        {
            left++;
            right++;
        }
        if (*left == 0 && *right == 0)
            return i;
    }
    return I_ENTBL;
}

static void build_table_filename(int table_index, char *filename,
    const char *extension)
{
    int index;
    const char *name;

    index = 0;
    name = g_cfg.tbls[table_index].name;
    while (index < 8 && *name)
    {
        filename[index] = *name;
        index++;
        name++;
    }
    filename[index] = '.';
    filename[index + 1] = extension[0];
    filename[index + 2] = extension[1];
    filename[index + 3] = extension[2];
    filename[index + 4] = 0;
}

static void close_cached_fd(int *file_descriptor)
{
    if (*file_descriptor != ERROR)
    {
        fd_slots[*file_descriptor] = -1;
        if (engine_close(*file_descriptor) != 0)
            cached_close_error = I_EWRIT;
        *file_descriptor = ERROR;
    }
}

static void close_table_files(void)
{
    int i;
    for (i = 0; i < FILE_CACHE_SIZE; ++i)
    {
        if (files_initialized)
            close_cached_fd(&file_cache[i].fd);
        file_cache[i].fd = ERROR;
        file_cache[i].references = 0;
        file_cache[i].age = 0;
    }
    files_initialized = 1;
    file_clock = 0;
    memset(file_slots, 0xff, sizeof(file_slots));
    memset(fd_slots, 0xff, sizeof(fd_slots));
#ifdef ISAM_BANK_LIBRARY
    memset(header_known, 0, sizeof(header_known));
#endif
}

static void invalidate_table_files(void)
{
    close_table_files();
#ifdef ISAM_BANK_LIBRARY
    discard_pages(-1);
#endif
}

static void index_extension(int key_index, char *extension)
{
    extension[0] = 'I';
    if (key_index == 0)
    {
        extension[1] = 'D';
        extension[2] = 'X';
    }
    else
    {
        extension[1] = 'X';
        extension[2] = '0' + key_index;
    }
    extension[3] = 0;
}

static int open_table_file(int table_index, const char *extension, int flags)
{
    char filename[20];
    int index_key;
    int i;
    int slot;
    int identity;

    if (!files_initialized)
        invalidate_table_files();
    index_key = -1;
    if (extension[0] == 'I')
        index_key = extension[1] == 'D' ? 0 : extension[2] - '0';
    identity = table_index * (I_MXKEY + 1) + index_key + 1;
    slot = file_slots[identity];

    if (flags & (O_CREAT | O_TRUNC))
    {
        if (slot >= 0)
            close_cached_fd(&file_cache[slot].fd);
        file_slots[identity] = -1;
        build_table_filename(table_index, filename, extension);
        return open(filename, flags, 0);
    }
    if (slot < 0)
    {
        for (i = 0; i < FILE_CACHE_SIZE; ++i)
        {
            if (file_cache[i].fd == ERROR)
            {
                slot = i;
                break;
            }
            if (!file_cache[i].references &&
                (slot < 0 || file_cache[i].age < file_cache[slot].age))
                slot = i;
        }
        if (slot < 0)
            return ERROR;
        if (file_cache[slot].fd != ERROR)
            file_slots[file_cache[slot].identity] = -1;
        close_cached_fd(&file_cache[slot].fd);
        file_cache[slot].references = 0;
        file_cache[slot].identity = identity;
#ifdef ISAM_BANK_LIBRARY
        header_known[slot] = 0;
#endif
        build_table_filename(table_index, filename, extension);
        file_cache[slot].fd = open(filename, O_RDWR);
        if (file_cache[slot].fd == ERROR)
            return ERROR;
        file_slots[identity] = slot;
        fd_slots[file_cache[slot].fd] = slot;
#ifdef ISAM_BANK_LIBRARY
        page_files[file_cache[slot].fd] = identity;
#endif
    }
    if (file_clock == 65535U)
    {
        for (i = 0; i < FILE_CACHE_SIZE; ++i)
            file_cache[i].age >>= 1;
        file_clock >>= 1;
    }
    file_cache[slot].age = ++file_clock;
    file_cache[slot].references++;
    return file_cache[slot].fd;
}

static int open_index_file(int table_index, int key_index, int flags)
{
    char extension[4];

    index_extension(key_index, extension);
    return open_table_file(table_index, extension, flags);
}

static void close_table_file(int file_descriptor)
{
    int slot;
    if (file_descriptor >= 3 && file_descriptor < 11)
    {
        slot = fd_slots[file_descriptor];
        if (slot >= 0)
        {
            if (file_cache[slot].references > 0)
                file_cache[slot].references--;
            return;
        }
    }
    engine_close(file_descriptor);
}

static void write_little_endian_16(char *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
}

static uint16_t read_little_endian_16(const char *buffer)
{
    return (uint16_t)(uint8_t)buffer[0] |
        ((uint16_t)(uint8_t)buffer[1] << 8);
}

static void encode_metadata(char *buffer,
    const struct persistent_metadata *metadata)
{
    write_little_endian_16(buffer, metadata->index_count);
    write_little_endian_16(buffer + 2, metadata->active_count);
    write_little_endian_16(buffer + 4, metadata->high_water_slot);
    write_little_endian_16(buffer + 6,
        metadata->free_head < 0 ? NO_FREE_SLOT : metadata->free_head);
    write_little_endian_16(buffer + 8, metadata->generation);
}

static void decode_metadata(const char *buffer,
    struct persistent_metadata *metadata)
{
    metadata->index_count = read_little_endian_16(buffer);
    metadata->active_count = read_little_endian_16(buffer + 2);
    metadata->high_water_slot = read_little_endian_16(buffer + 4);
    metadata->free_head = read_little_endian_16(buffer + 6);
    if (metadata->free_head == NO_FREE_SLOT)
        metadata->free_head = -1;
    metadata->generation = read_little_endian_16(buffer + 8);
}

static int metadata_checksum(const char *header)
{
    unsigned char count;
    unsigned int checksum;
    const unsigned char *bytes;

    checksum = 0;
    bytes = (const unsigned char *)header;
    count = 20;
    while (count--)
        checksum += *bytes++;
    return checksum;
}

static int read_index_header(int fd, int tidx, int key_index,
    struct persistent_metadata *metadata)
{
    char hdr[PRIMARY_INDEX_HEADER_SIZE];
#ifdef ISAM_BANK_LIBRARY
    int slot;

    slot = fd_slots[fd];
    /* Schema changes close all descriptors before this cache can be reused. */
    if (slot >= 0 && header_known[slot])
    {
        *metadata = header_cache[slot];
        return I_OK;
    }
#endif

    if (engine_seek(fd, 0L, 0) < 0L)
        return I_EINDEX;
    if (engine_read(fd, hdr, PRIMARY_INDEX_HEADER_SIZE) != PRIMARY_INDEX_HEADER_SIZE)
        return I_EINDEX;
    if (hdr[0] != PRIMARY_INDEX_MAGIC_0 || hdr[1] != PRIMARY_INDEX_MAGIC_1 ||
        hdr[2] != PRIMARY_INDEX_MAGIC_2 || hdr[3] != PRIMARY_INDEX_MAGIC_3)
        return I_EINDEX;
    if ((unsigned char)hdr[4] != g_cfg.tbls[tidx].keyoff[key_index] ||
        (unsigned char)hdr[5] != g_cfg.tbls[tidx].tails[key_index] ||
        (unsigned char)hdr[6] != g_cfg.tbls[tidx].keysz[key_index] ||
        (unsigned char)hdr[7] != g_cfg.tbls[tidx].extra[key_index] ||
        read_little_endian_16(hdr + 8) != g_cfg.tbls[tidx].recsz)
        return I_EINDEX;
    if (read_little_endian_16(hdr + 20) != metadata_checksum(hdr))
        return I_EINDEX;
    decode_metadata(hdr + 10, metadata);
    if (metadata->index_count < 0 || metadata->active_count < 0 ||
        metadata->high_water_slot < metadata->active_count ||
        metadata->free_head < -1 ||
        metadata->free_head >= metadata->high_water_slot)
        return I_EINDEX;
#ifdef ISAM_BANK_LIBRARY
    if (slot >= 0)
    {
        header_cache[slot] = *metadata;
        header_known[slot] = 1;
    }
#endif
    return I_OK;
}

static int write_index_header(int fd, int tidx, int key_index,
    const struct persistent_metadata *metadata)
{
    char hdr[PRIMARY_INDEX_HEADER_SIZE];
#ifdef ISAM_BANK_LIBRARY
    int slot;

    slot = fd_slots[fd];
    if (slot >= 0)
        header_known[slot] = 0;
#endif

    memset(hdr, 0, PRIMARY_INDEX_HEADER_SIZE);
    hdr[0] = PRIMARY_INDEX_MAGIC_0;
    hdr[1] = PRIMARY_INDEX_MAGIC_1;
    hdr[2] = PRIMARY_INDEX_MAGIC_2;
    hdr[3] = PRIMARY_INDEX_MAGIC_3;
    hdr[4] = g_cfg.tbls[tidx].keyoff[key_index];
    hdr[5] = g_cfg.tbls[tidx].tails[key_index];
    hdr[6] = g_cfg.tbls[tidx].keysz[key_index];
    hdr[7] = g_cfg.tbls[tidx].extra[key_index];
    write_little_endian_16(hdr + 8, g_cfg.tbls[tidx].recsz);
    encode_metadata(hdr + 10, metadata);
    write_little_endian_16(hdr + 20, metadata_checksum(hdr));
    if (engine_seek(fd, 0L, 0) < 0L)
        return I_EWRIT;
    if (engine_write(fd, hdr, PRIMARY_INDEX_HEADER_SIZE) != PRIMARY_INDEX_HEADER_SIZE)
        return I_EWRIT;
    return I_OK;
}

static int load_table_metadata(int table_index,
    struct persistent_metadata *metadata)
{
    int file_descriptor;
    int rc;

    file_descriptor = open_index_file(table_index, 0, O_RDONLY);
    if (file_descriptor == ERROR)
        return I_EINDEX;
    rc = read_index_header(file_descriptor, table_index, 0, metadata);
    close_table_file(file_descriptor);
    if (rc == I_OK)
    {
        g_cfg.tbls[table_index].idxcnt = metadata->index_count;
        g_cfg.tbls[table_index].nrecs = metadata->active_count;
        g_cfg.tbls[table_index].maxrec = metadata->high_water_slot;
    }
    return rc;
}

static int save_table_metadata(int table_index,
    const struct persistent_metadata *metadata)
{
    int file_descriptor;
    int rc;

    file_descriptor = open_index_file(table_index, 0, O_RDWR);
    if (file_descriptor == ERROR)
        return I_EINDEX;
    rc = write_index_header(file_descriptor, table_index, 0, metadata);
    if (rc == I_OK && fsync(file_descriptor) != 0)
        rc = I_EWRIT;
    close_table_files();
    if (rc == I_OK)
    {
        g_cfg.tbls[table_index].idxcnt = metadata->index_count;
        g_cfg.tbls[table_index].nrecs = metadata->active_count;
        g_cfg.tbls[table_index].maxrec = metadata->high_water_slot;
    }
    return rc;
}

static int read_free_link(const char *record)
{
    int next_slot;

    next_slot = read_little_endian_16(record + 1);
    return next_slot == NO_FREE_SLOT ? -1 : next_slot;
}

static void set_free_link(char *record, int next_slot)
{
    write_little_endian_16(record + 1,
        next_slot < 0 ? NO_FREE_SLOT : next_slot);
}

static int rebuild_free_link(int table_index, int physical_slot, int next_slot)
{
    int file_descriptor;
    int rc;
    long offset;
    char link[2];

    write_little_endian_16(link,
        next_slot < 0 ? NO_FREE_SLOT : next_slot);
    file_descriptor = open_table_file(table_index, "DAT", O_RDWR);
    if (file_descriptor == ERROR)
        return I_EOPEN;
    offset = (long)physical_slot * g_cfg.tbls[table_index].recsz + 1L;
    rc = I_OK;
    if (engine_seek(file_descriptor, offset, 0) < 0L ||
        engine_write(file_descriptor, link, 2) != 2)
        rc = I_EWRIT;
    if (rc == I_OK && fsync(file_descriptor) != 0)
        rc = I_EWRIT;
    close_table_file(file_descriptor);
    return rc;
}

int pki_create(int tblnam)
{
    int fd;
    int key_index;
    int rc;
    int tidx;
    char extension[4];
    char fname[20];
    struct persistent_metadata metadata;

    tidx = checked_table(tblnam);
    if (tidx < 0)
        return tidx;
    invalidate_table_files();
    if (valid_config(&g_cfg) != I_OK)
        return I_ESIZE;
    metadata.index_count = 0;
    metadata.active_count = 0;
    metadata.high_water_slot = 0;
    metadata.free_head = -1;
    metadata.generation = 0;
    rc = I_OK;
    for (key_index = 0; key_index < g_cfg.tbls[tidx].nkeys; key_index++)
    {
        index_extension(key_index, extension);
        build_table_filename(tidx, fname, extension);
        fd = open(fname, O_CREAT | O_TRUNC | O_RDWR, 0);
        if (fd == ERROR)
            return I_EOPEN;
        rc = write_index_header(fd, tidx, key_index, &metadata);
        if (rc == I_OK && fsync(fd) != 0)
            rc = I_EWRIT;
        engine_close(fd);
        if (rc != I_OK)
            return rc;
    }
    if (rc == I_OK)
    {
        build_table_filename(tidx, fname, "MAP");
        unlink(fname);
        g_cfg.tbls[tidx].nrecs = 0;
        g_cfg.tbls[tidx].maxrec = 0;
        g_cfg.tbls[tidx].idxcnt = 0;
    }
    return rc;
}

int pki_load(int table_name)
{
    int file_descriptor;
    int key_index;
    int rc;
    int table_index;
    struct persistent_metadata metadata;
    struct persistent_metadata secondary_metadata;

    table_index = checked_table(table_name);
    if (table_index < 0)
        return table_index;
    if (build_pending(table_index))
        return I_EBUILD;
    rc = load_table_metadata(table_index, &metadata);
    if (rc != I_OK)
        return rc;
    for (key_index = 1; key_index < g_cfg.tbls[table_index].nkeys;
        key_index++)
    {
        file_descriptor = open_index_file(table_index, key_index, O_RDONLY);
        if (file_descriptor == ERROR)
            return pki_rebuild(table_name);
        rc = read_index_header(file_descriptor, table_index, key_index,
            &secondary_metadata);
        close_table_file(file_descriptor);
        if (rc != I_OK || secondary_metadata.index_count <
            metadata.active_count)
            return pki_rebuild(table_name);
    }
    return I_OK;
}

static long index_entry_offset(int width, int position)
{
    if ((unsigned int)position <=
        (65535U - PRIMARY_INDEX_HEADER_SIZE) / (I_MXKEYLN + 2))
        return (unsigned long)((unsigned int)PRIMARY_INDEX_HEADER_SIZE +
            (unsigned int)position * width);
    return (long)PRIMARY_INDEX_HEADER_SIZE +
        ((long)position * width);
}

static int read_index_entry(int fd, int tidx, int key_index, int position,
    char *key, int *phys)
{
    int ksz;

    ksz = g_cfg.tbls[tidx].keysz[key_index];
    /* A negative position continues an already positioned index stream. */
    if (position >= 0 &&
        engine_seek(fd, index_entry_offset(ksz + 2, position), 0) < 0L)
        return I_EREAD;
    if (engine_read(fd, cursor_key, ksz + 2) != ksz + 2)
        return I_EREAD;
    if (key != 0)
        memcpy(key, cursor_key, ksz);
    *phys = read_little_endian_16(cursor_key + ksz);
    return I_OK;
}

static int write_index_entry(int fd, int tidx, int key_index, int position,
    const char *key, int phys)
{
    int ksz;
    char ent[I_MXKEYLN + 2];

    ksz = g_cfg.tbls[tidx].keysz[key_index];
    memcpy(ent, key, ksz);
    write_little_endian_16(ent + ksz, phys);
    if (engine_seek(fd, index_entry_offset(ksz + 2, position), 0) < 0L)
        return I_EWRIT;
    if (engine_write(fd, ent, ksz + 2) != ksz + 2)
        return I_EWRIT;
    return I_OK;
}

static int move_index_entries(int file_descriptor, int table_index,
    int key_index, int first_position, int entry_count, int direction)
{
    int block_entries;
    int block_size;
    int entry_size;
    int source_position;

    entry_size = g_cfg.tbls[table_index].keysz[key_index] + 2;
    block_entries = I_RECSZ / entry_size;
    while (entry_count > 0)
    {
        if (block_entries > entry_count)
            block_entries = entry_count;
        if (direction > 0)
            source_position = first_position + entry_count - block_entries;
        else
            source_position = first_position;
        block_size = block_entries * entry_size;
        if (engine_seek(file_descriptor,
            index_entry_offset(entry_size, source_position), 0) < 0L ||
            engine_read(file_descriptor, record_workspace, block_size) != block_size ||
            engine_seek(file_descriptor,
                index_entry_offset(entry_size, source_position + direction),
                0) < 0L ||
            engine_write(file_descriptor, record_workspace, block_size) != block_size)
            return I_EWRIT;
        entry_count -= block_entries;
        if (direction < 0)
            first_position += block_entries;
    }
    return I_OK;
}

static int compare_keys(const char *left, const char *right, int len)
{
    return memcmp(left, right, len);
}

/* Return the first entry whose key is greater than or equal to key. */
static int find_lower_bound(int fd, int tidx, int key_index, int count,
    const char *key, int *position, int *found, int *matched)
{
    int cmp;
    int hi;
    int lo;
    int mid;
    int phys;
    int rc;
    char entkey[I_MXKEYLN];

    lo = 0;
    hi = count;
    *found = 0;
    while (lo < hi)
    {
        stats_add(&i_runtime_stats.index_comparisons, 1);
        mid = lo + ((unsigned int)(hi - lo) >> 1);
        rc = read_index_entry(fd, tidx, key_index, mid, entkey, &phys);
        if (rc != I_OK)
            return rc;
        cmp = compare_keys(entkey, key, g_cfg.tbls[tidx].keysz[key_index]);
        if (cmp == 0)
        {
            *found = 1;
            *position = mid;
            *matched = phys;
            return I_OK;
        }
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    *position = lo;
    return I_OK;
}

static int lookup_slot(int tidx, int key_index, int fd, int count, const char *key)
{
    int found, phys, rc, width;
    stats_add(&i_runtime_stats.exact_lookups, 1);
    found = 0;
    if (lookup_position >= 0 && lookup_position < count)
    {
        rc = read_index_entry(fd, tidx, key_index, lookup_position, 0, &phys);
        if (rc != I_OK)
            return rc;
        width = g_cfg.tbls[tidx].keysz[key_index];
        stats_add(&i_runtime_stats.index_comparisons, 1);
        found = !compare_keys(cursor_key, key, width);
    }
    if (!found)
        rc = find_lower_bound(fd, tidx, key_index, count, key,
            &lookup_position, &found, &phys);
    else
        rc = I_OK;
    if (rc != I_OK)
        return rc;
    if (!found || phys == DELETED_INDEX_SLOT)
        return I_ENREC;
    return phys < 0 || phys >= g_cfg.tbls[tidx].maxrec ? I_EINDEX : phys;
}

int idx_find(int table, int key_index, const char *key, char *rec)
{
    int fd, slot, rc;
    struct persistent_metadata metadata;
    if (checked_table(table) < 0)
        return I_ENTBL;
    if (key_index < 0 || key_index >= g_cfg.tbls[table].nkeys)
        return I_EINDEX;
    fd = open_index_file(table, key_index, O_RDONLY);
    if (fd == ERROR)
        return I_EINDEX;
    rc = read_index_header(fd, table, key_index, &metadata);
    lookup_position = -1;
    slot = rc == I_OK ? lookup_slot(table, key_index, fd,
        metadata.index_count, key) : rc;
    close_table_file(fd);
    if (slot >= 0 && rec)
    {
        rc = i_rdphys(table, rec, slot);
        if (rc != I_OK)
            return I_EINDEX;
    }
    return slot;
}

int key_find(int tblnam, const char *key, char *rec)
{
    return idx_find(tblnam, 0, key, rec);
}

static int prepare_key_changes(int tidx, const char *oldrec,
    const char *newrec, int phys, struct key_change *changes)
{
    int fd;
    int found;
    int entry_phys;
    int key_index;
    int rc;
    char oldkey[I_MXKEYLN];
    struct key_change *change;
    const struct i_tbl *table;

    table = &g_cfg.tbls[tidx];
    for (key_index = 0; key_index < table->nkeys; key_index++)
    {
        change = &changes[key_index];
        change->position = -1;
        change->old_position = -1;
        make_index_key(tidx, key_index, newrec, change->key);
        if (oldrec)
        {
            make_index_key(tidx, key_index, oldrec, oldkey);
            if (!compare_keys(oldkey, change->key, table->keysz[key_index]))
                continue;
        }
        fd = open_index_file(tidx, key_index, O_RDWR);
        if (fd == ERROR)
            return I_EINDEX;
        rc = read_index_header(fd, tidx, key_index, &change->metadata);
        if (rc == I_OK)
            rc = find_lower_bound(fd, tidx, key_index,
                change->metadata.index_count, change->key,
                &change->position, &change->found, &entry_phys);
        if (rc == I_OK && change->found)
        {
            if (entry_phys != DELETED_INDEX_SLOT)
                rc = I_EDUP;
        }
        if (rc == I_OK && !oldrec && !change->found &&
            change->metadata.index_count == 32767)
            rc = I_EFULL;
        if (rc == I_OK && oldrec)
        {
            rc = find_lower_bound(fd, tidx, key_index,
                change->metadata.index_count, oldkey,
                &change->old_position, &found, &entry_phys);
            if (rc == I_OK && !found)
                rc = I_EINDEX;
            if (rc == I_OK && entry_phys != phys)
                rc = I_EINDEX;
        }
        close_table_file(fd);
        if (rc != I_OK)
            return rc;
    }
    return I_OK;
}

static int apply_index_change(int tidx, int key_index, const char *rec,
    int phys, struct key_change *change)
{
    int fd;
    int old_count;
    int position;
    int old_position;
    int rc;
    struct persistent_metadata *metadata;

    if (change->position < 0)
        return I_OK;
    fd = open_index_file(tidx, key_index, O_RDWR);
    if (fd == ERROR)
        return I_EINDEX;
    metadata = &change->metadata;
    old_count = metadata->index_count;
    position = change->position;
    old_position = change->old_position;
    rc = I_OK;
    if (old_position >= 0 && !change->found)
    {
        /* Rotate only the interval between the old and new key. */
        if (position > old_position)
        {
            rc = move_index_entries(fd, tidx, key_index, old_position + 1,
                position - old_position - 1, -1);
            position--;
        }
        else
            rc = move_index_entries(fd, tidx, key_index, position,
                old_position - position, 1);
    }
    else if (old_position >= 0)
    {
        rc = move_index_entries(fd, tidx, key_index, old_position + 1,
            old_count - old_position - 1, -1);
        metadata->index_count--;
        if (position > old_position)
            position--;
    }
    else if (!change->found)
    {
        rc = move_index_entries(fd, tidx, key_index, position,
            old_count - position, 1);
        metadata->index_count++;
    }
    if (rc == I_OK)
        rc = write_index_entry(fd, tidx, key_index, position,
            change->key, phys);
    if (rc == I_OK && old_count != metadata->index_count)
        rc = write_index_header(fd, tidx, key_index, metadata);
    if (rc == I_OK && fsync(fd) != 0)
        rc = I_EWRIT;
    if (!defer_table_file_invalidation)
        invalidate_table_files();
    else
        close_table_file(fd);
    if (rc == I_OK && key_index == 0)
        g_cfg.tbls[tidx].idxcnt = metadata->index_count;
    return rc;
}

static int apply_key_changes(int tidx, const char *rec, int phys,
    struct key_change *changes)
{
    int key_index;
    int rc;

    for (key_index = 0; key_index < g_cfg.tbls[tidx].nkeys; key_index++)
    {
        rc = apply_index_change(tidx, key_index, rec, phys,
            &changes[key_index]);
        if (rc != I_OK)
            return rc;
    }
    return I_OK;
}

int key_add(int tblnam, const char *rec, int phys)
{
    const char *saved_record;
    int rc;
    int tidx;

    tidx = checked_table(tblnam);
    if (tidx < 0)
        return tidx;
    if (rec == record_workspace)
    {
        memcpy(key_record_workspace, rec, g_cfg.tbls[tidx].recsz);
        saved_record = key_record_workspace;
    }
    else
        saved_record = rec;
    rc = prepare_key_changes(tidx, 0, saved_record, phys, key_changes);
    if (rc != I_OK)
        return rc;
    return apply_key_changes(tidx, saved_record, phys, key_changes);
}

static int erase_index_key(int tidx, int key_index, const char *key,
    int phys, int tombstone)
{
    int entphys;
    int fd;
    int found;
    int position;
    int rc;
    struct persistent_metadata metadata;

    fd = open_index_file(tidx, key_index, O_RDWR);
    if (fd == ERROR)
        return I_EINDEX;
    rc = read_index_header(fd, tidx, key_index, &metadata);
    if (rc == I_OK)
        rc = find_lower_bound(fd, tidx, key_index, metadata.index_count, key,
            &position, &found, &entphys);
    if (rc == I_OK && !found)
        rc = I_EINDEX;
    if (rc == I_OK && entphys != phys)
        rc = I_EINDEX;
    if (rc == I_OK)
    {
        if (tombstone)
            rc = write_index_entry(fd, tidx, key_index, position, key,
                DELETED_INDEX_SLOT);
        else
        {
            rc = move_index_entries(fd, tidx, key_index, position + 1,
                metadata.index_count - position - 1, -1);
            if (rc == I_OK)
            {
                metadata.index_count--;
                rc = write_index_header(fd, tidx, key_index, &metadata);
            }
        }
    }
    if (rc == I_OK && fsync(fd) != 0)
        rc = I_EWRIT;
    if (!defer_table_file_invalidation)
        invalidate_table_files();
    else
        close_table_file(fd);
    if (rc == I_OK && key_index == 0 && !tombstone)
        g_cfg.tbls[tidx].idxcnt = metadata.index_count;
    return rc;
}

static int erase_keys(int tidx, const char *rec, int phys, int tombstone)
{
    int key_index;
    int rc;
    char key[I_MXKEYLN];

    for (key_index = 0; key_index < g_cfg.tbls[tidx].nkeys; key_index++)
    {
        make_index_key(tidx, key_index, rec, key);
        rc = erase_index_key(tidx, key_index, key, phys, tombstone);
        if (rc != I_OK)
            return rc;
    }
    return I_OK;
}

int key_remove(int tblnam, const char *rec, int phys)
{
    int tidx;
    tidx = checked_table(tblnam);
    return tidx < 0 ? tidx : erase_keys(tidx, rec, phys, 0);
}

int key_update(int tblnam, const char *oldrec,
    const char *newrec, int phys)
{
    int rc;
    int tidx;

    tidx = checked_table(tblnam);
    if (tidx < 0)
        return tidx;
    rc = prepare_key_changes(tidx, oldrec, newrec, phys, key_changes);
    if (rc != I_OK)
        return rc;
    return apply_key_changes(tidx, newrec, phys, key_changes);
}

/* Negative position continues the stream; call boundaries always seek first. */
static int read_cursor_slot(int fd, struct i_cursor *cursor, int position)
{
    int phys, rc;
    phys = DELETED_INDEX_SLOT;
    while (cursor->position >= 0 &&
        cursor->position < cursor->entry_count &&
        phys == DELETED_INDEX_SLOT)
    {
        rc = read_index_entry(fd, cursor->table_index, cursor->key_index,
            position, 0, &phys);
        if (rc != I_OK)
            return rc;
        position = -1;
        if (phys != DELETED_INDEX_SLOT)
            break;
        cursor->position++;
    }
    if (phys == DELETED_INDEX_SLOT)
        return I_ENREC;
    if (cursor->prefix_length > 0 && compare_keys(cursor_key, cursor->prefix,
        cursor->prefix_length) != 0)
        return I_ENREC;
    if (phys < 0 || phys >= g_cfg.tbls[cursor->table_index].maxrec)
        return I_EINDEX;
    stats_add(&i_runtime_stats.cursor_rows, 1);
    return phys;
}

static int read_cursor_record(int table, struct i_cursor *cursor, char *rec)
{
    int fd, slot;
    fd = open_index_file(table, cursor->key_index, O_RDONLY);
    if (fd == ERROR)
        return I_EINDEX;
    slot = read_cursor_slot(fd, cursor, cursor->position);
    close_table_file(fd);
    if (slot >= 0 && rec && i_rdphys(table, rec, slot) != I_OK)
        return I_EINDEX;
    return slot;
}

int idx_open(int tblnam, int key_index, const char *prefix,
    int prefix_length, struct i_cursor *cursor, char *rec)
{
    int fd;
    int found;
    int rc;
    int tidx, matched;
    char lower_key[I_MXKEYLN];
    struct persistent_metadata metadata;

    stats_add(&i_runtime_stats.cursor_starts, 1);
    tidx = checked_table(tblnam);
    if (tidx < 0)
        return tidx;
    if (key_index < 0 || key_index >= g_cfg.tbls[tidx].nkeys ||
        prefix_length < 0 || prefix_length > g_cfg.tbls[tidx].keysz[key_index])
        return I_EINDEX;
    fd = open_index_file(tidx, key_index, O_RDONLY);
    if (fd == ERROR)
        return I_EINDEX;
    rc = read_index_header(fd, tidx, key_index, &metadata);
    cursor->position = 0;
    if (rc == I_OK && prefix_length > 0)
    {
        memset(lower_key, 0, g_cfg.tbls[tidx].keysz[key_index]);
        memcpy(lower_key, prefix, prefix_length);
        rc = find_lower_bound(fd, tidx, key_index, metadata.index_count,
            lower_key, &cursor->position, &found, &matched);
    }
    close_table_file(fd);
    if (rc != I_OK)
        return rc;
    cursor->entry_count = metadata.index_count;
    cursor->table_index = tidx;
    cursor->key_index = key_index;
    cursor->prefix_length = prefix_length;
    if (prefix_length > 0)
        memcpy(cursor->prefix, prefix, prefix_length);
    return read_cursor_record(tblnam, cursor, rec);
}

int cur_first(int tblnam, struct i_cursor *cursor, char *rec)
{
    return idx_open(tblnam, 0, 0, 0, cursor, rec);
}

int cur_next(int tblnam, struct i_cursor *cursor, char *rec)
{
    cursor->position++;
    return read_cursor_record(tblnam, cursor, rec);
}

int pki_rebuild(int tblnam)
{
    int rc;
    int tidx;
    int high_water_slot;
    int generation;
    struct persistent_metadata metadata;

    invalidate_table_files();
    tidx = checked_table(tblnam);
    if (tidx < 0)
        return tidx;
    rc = load_table_metadata(tidx, &metadata);
    if (rc == I_OK)
    {
        high_water_slot = metadata.high_water_slot;
        generation = metadata.generation + 1;
    }
    else if (rc == I_EINDEX)
    {
        high_water_slot = g_cfg.tbls[tidx].maxrec;
        generation = 1;
    }
    else
        return rc;
    return build_indexes(tidx, high_water_slot, generation);
}

static int read_data_slot(int file_descriptor, int record_size,
    int physical_slot, char *record)
{
    long offset;

    offset = (long)physical_slot * record_size;
    if (engine_seek(file_descriptor, offset, 0) < 0L)
        return I_EREAD;
    if (engine_read(file_descriptor, record, record_size) != record_size)
        return I_EREAD;
    return I_OK;
}

int table_verify(int table_name, struct i_verify *report)
{
    int data_file;
    int free_slot;
    int index;
    int index_file;
    int index_entries;
    int key_index;
    int next_slot;
    int physical_slot;
    int rc;
    int table_index;
    char key[I_MXKEYLN];
    char previous_key[I_MXKEYLN];
    char expected_key[I_MXKEYLN];
    char *record;
    struct persistent_metadata metadata;
    struct persistent_metadata index_metadata;

    /* Verification must also see corruption introduced outside this session. */
    invalidate_table_files();
    record = record_workspace;
    report->index_entries = 0;
    report->active_records = 0;
    report->deleted_records = 0;
    report->free_slots = 0;
    report->errors = 0;
    table_index = checked_table(table_name);
    if (table_index < 0)
        return table_index;
    rc = load_table_metadata(table_index, &metadata);
    if (rc != I_OK)
        return rc;
    data_file = open_table_file(table_index, "DAT", O_RDONLY);
    if (data_file == ERROR)
        return I_EINDEX;

    for (key_index = 0; key_index < g_cfg.tbls[table_index].nkeys;
        key_index++)
    {
        index_file = open_index_file(table_index, key_index, O_RDONLY);
        if (index_file == ERROR || read_index_header(index_file, table_index,
            key_index, &index_metadata) != I_OK)
        {
            report->errors++;
            break;
        }
        index_entries = 0;
        for (index = 0; index < index_metadata.index_count; index++)
        {
            rc = read_index_entry(index_file, table_index, key_index, index,
                key, &physical_slot);
            if (rc != I_OK)
            {
                report->errors++;
                break;
            }
            if (physical_slot != DELETED_INDEX_SLOT)
            {
                index_entries++;
                if (physical_slot < 0 ||
                    physical_slot >= metadata.high_water_slot ||
                    read_data_slot(data_file, g_cfg.tbls[table_index].recsz,
                        physical_slot, record) != I_OK ||
                    record[0] == I_DELFLAG)
                    report->errors++;
                else
                {
                    make_index_key(table_index, key_index, record, expected_key);
                    if (compare_keys(key, expected_key,
                        g_cfg.tbls[table_index].keysz[key_index]))
                        report->errors++;
                }
            }
            if (index > 0 && compare_keys(previous_key, key,
                g_cfg.tbls[table_index].keysz[key_index]) >= 0)
                report->errors++;
            memcpy(previous_key, key,
                g_cfg.tbls[table_index].keysz[key_index]);
        }
        if (key_index == 0)
            report->index_entries = index_entries;
        if (index_entries != metadata.active_count)
            report->errors++;
        close_table_file(index_file);
    }

    for (physical_slot = 0; physical_slot < metadata.high_water_slot;
        physical_slot++)
    {
        if (read_data_slot(data_file, g_cfg.tbls[table_index].recsz,
            physical_slot, record) != I_OK)
        {
            report->errors++;
            break;
        }
        if (record[0] == I_DELFLAG)
            report->deleted_records++;
        else
            report->active_records++;
    }

    free_slot = metadata.free_head;
    while (free_slot >= 0 && report->free_slots <= metadata.high_water_slot)
    {
        if (free_slot >= metadata.high_water_slot ||
            read_data_slot(data_file, g_cfg.tbls[table_index].recsz,
                free_slot, record) != I_OK || record[0] != I_DELFLAG)
        {
            report->errors++;
            break;
        }
        next_slot = read_free_link(record);
        free_slot = next_slot;
        report->free_slots++;
    }
    if (free_slot < -1 || report->free_slots > metadata.high_water_slot)
        report->errors++;
    if (metadata.index_count < metadata.active_count ||
        metadata.active_count != report->index_entries ||
        metadata.active_count != report->active_records ||
        report->deleted_records != report->free_slots ||
        metadata.active_count + report->deleted_records !=
            metadata.high_water_slot)
        report->errors++;

    close_table_file(data_file);
    return report->errors == 0 ? I_OK : I_EVERIFY;
}