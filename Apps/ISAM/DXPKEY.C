#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "ISAMDB.H"

#define PRIMARY_INDEX_HEADER_SIZE 24
#define PRIMARY_INDEX_MAGIC_0 'D'
#define PRIMARY_INDEX_MAGIC_1 'X'
#define PRIMARY_INDEX_MAGIC_2 'I'
#define PRIMARY_INDEX_MAGIC_3 '3'
#define NO_FREE_SLOT 0xffff
#define DELETED_INDEX_SLOT 0xffff
#define JOURNAL_HEADER_SIZE 24
#define JOURNAL_STATE_PREPARED 1
#define JOURNAL_STATE_COMMITTED 2
#define JOURNAL_OPERATION_INSERT 1
#define JOURNAL_OPERATION_UPDATE 2
#define JOURNAL_OPERATION_DELETE 3

struct persistent_metadata
{
    int index_count;
    int active_count;
    int high_water_slot;
    int free_head;
    int generation;
};

#ifndef DXISAM_DISABLE_JOURNAL
struct journal_record
{
    int state;
    int operation;
    int physical_slot;
    int old_record_exists;
    struct persistent_metadata old_metadata;
    char old_record[I_RECSZ];
};

static struct journal_record active_journal;
#endif
static char record_workspace[I_RECSZ];
static char key_record_workspace[I_RECSZ];
#ifndef DXISAM_DISABLE_JOURNAL
static int crash_after_phase;
#endif
static int cached_table_index = -1;
static int cached_data_fd = ERROR;
static int cached_index_fd = ERROR;
static int cached_index_key = -1;
static int defer_table_file_invalidation;

#ifndef DXISAM_DISABLE_JOURNAL
static int recover_table_journal(int table_index);
#else
#define clear_journal(table_index) I_OK
#define write_journal(table_index, operation, physical_slot, old_exists, old_record, old_metadata) I_OK
#define commit_journal(table_index) I_OK
#define simulate_crash_after(phase) 0
#define abort_journal(table_index, operation_error) operation_error
#define recover_table_journal(table_index) I_OK
#endif

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
        close(*file_descriptor);
        *file_descriptor = ERROR;
    }
}

static void invalidate_table_files(void)
{
    close_cached_fd(&cached_data_fd);
    close_cached_fd(&cached_index_fd);
    cached_table_index = -1;
    cached_index_key = -1;
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

    index_key = -1;
    if (extension[0] == 'I')
        index_key = extension[1] == 'D' ? 0 : extension[2] - '0';

    if (flags & (O_CREAT | O_TRUNC))
    {
        if (cached_table_index == table_index)
        {
            if (extension[0] == 'D')
                close_cached_fd(&cached_data_fd);
            else if (index_key >= 0)
                close_cached_fd(&cached_index_fd);
        }
        build_table_filename(table_index, filename, extension);
        return open(filename, flags, 0);
    }
    if (cached_table_index != table_index)
    {
        invalidate_table_files();
        cached_table_index = table_index;
    }
    if (extension[0] == 'D')
    {
        if (cached_data_fd == ERROR)
        {
            build_table_filename(table_index, filename, extension);
            cached_data_fd = open(filename, O_RDWR);
        }
        return cached_data_fd;
    }
    if (cached_index_key != index_key)
    {
        close_cached_fd(&cached_index_fd);
        cached_index_key = index_key;
    }
    if (cached_index_fd == ERROR)
    {
        build_table_filename(table_index, filename, extension);
        cached_index_fd = open(filename, O_RDWR);
    }
    return cached_index_fd;
}

static int open_index_file(int table_index, int key_index, int flags)
{
    char extension[4];

    index_extension(key_index, extension);
    return open_table_file(table_index, extension, flags);
}

static void close_table_file(int file_descriptor)
{
    if (file_descriptor != cached_data_fd && file_descriptor != cached_index_fd)
        close(file_descriptor);
}

static void remove_legacy_table_file(int table_index, const char *extension)
{
    int index;
    char filename[20];
    const char *name;

    filename[0] = g_cfg.tbls[table_index].disk;
    filename[1] = ':';
    index = 0;
    name = g_cfg.tbls[table_index].name;
    while (index < 8 && *name)
    {
        filename[index + 2] = *name;
        index++;
        name++;
    }
    filename[index + 2] = '.';
    filename[index + 3] = extension[0];
    filename[index + 4] = extension[1];
    filename[index + 5] = extension[2];
    filename[index + 6] = 0;
    unlink(filename);
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

#ifndef DXISAM_DISABLE_JOURNAL
static void encode_metadata(char *buffer, int offset,
    const struct persistent_metadata *metadata)
{
    write_little_endian_16(buffer + offset, metadata->index_count);
    write_little_endian_16(buffer + offset + 2, metadata->active_count);
    write_little_endian_16(buffer + offset + 4, metadata->high_water_slot);
    write_little_endian_16(buffer + offset + 6,
        metadata->free_head < 0 ? NO_FREE_SLOT : metadata->free_head);
    write_little_endian_16(buffer + offset + 8, metadata->generation);
}

static void decode_metadata(const char *buffer, int offset,
    struct persistent_metadata *metadata)
{
    metadata->index_count = read_little_endian_16(buffer + offset);
    metadata->active_count = read_little_endian_16(buffer + offset + 2);
    metadata->high_water_slot = read_little_endian_16(buffer + offset + 4);
    metadata->free_head = read_little_endian_16(buffer + offset + 6);
    if (metadata->free_head == NO_FREE_SLOT)
        metadata->free_head = -1;
    metadata->generation = read_little_endian_16(buffer + offset + 8);
}

static int journal_checksum(const char *header, const char *old_record,
    int record_size)
{
    unsigned char count;
    unsigned int checksum;
    const unsigned char *bytes;

    checksum = 0;
    bytes = (const unsigned char *)header;
    count = 4;
    while (count--)
        checksum += *bytes++;
    bytes++;
    count = 17;
    while (count--)
        checksum += *bytes++;
    bytes = (const unsigned char *)old_record;
    count = record_size;
    while (count--)
        checksum += *bytes++;
    return checksum;
}

static int clear_journal(int table_index)
{
    int file_descriptor;
    int rc;
    char filename[20];

    build_table_filename(table_index, filename, "JRN");
    file_descriptor = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0);
    if (file_descriptor == ERROR)
        return I_EOPEN;
    rc = fsync(file_descriptor) == 0 ? I_OK : I_EWRIT;
    close(file_descriptor);
    return rc;
}

static int write_journal(int table_index, int operation, int physical_slot,
    int old_record_exists, const char *old_record,
    const struct persistent_metadata *old_metadata)
{
    int file_descriptor;
    int record_size;
    int rc;
    char filename[20];
    char header[JOURNAL_HEADER_SIZE];

    memset(header, 0, JOURNAL_HEADER_SIZE);
    header[0] = 'D';
    header[1] = 'X';
    header[2] = 'J';
    header[3] = '1';
    header[4] = JOURNAL_STATE_PREPARED;
    header[5] = operation;
    record_size = g_cfg.tbls[table_index].recsz;
    write_little_endian_16(header + 6, record_size);
    write_little_endian_16(header + 8, physical_slot);
    write_little_endian_16(header + 10, old_record_exists);
    encode_metadata(header, 12, old_metadata);
    write_little_endian_16(header + 22,
        journal_checksum(header, old_record, record_size));
    build_table_filename(table_index, filename, "JRN");
    file_descriptor = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0);
    if (file_descriptor == ERROR)
        return I_EOPEN;
    rc = I_OK;
    if (write(file_descriptor, header, JOURNAL_HEADER_SIZE) !=
        JOURNAL_HEADER_SIZE ||
        write(file_descriptor, old_record, record_size) != record_size)
        rc = I_EWRIT;
    if (rc == I_OK && fsync(file_descriptor) != 0)
        rc = I_EWRIT;
    close(file_descriptor);
    return rc;
}

static int commit_journal(int table_index)
{
    int file_descriptor;
    int rc;
    char filename[20];
    char state;

    build_table_filename(table_index, filename, "JRN");
    file_descriptor = open(filename, O_RDWR);
    if (file_descriptor == ERROR)
        return I_EOPEN;
    state = JOURNAL_STATE_COMMITTED;
    rc = I_OK;
    if (lseek(file_descriptor, 4L, 0) < 0L ||
        write(file_descriptor, &state, 1) != 1 || fsync(file_descriptor) != 0)
        rc = I_EWRIT;
    close(file_descriptor);
    return rc;
}

static int read_journal(int table_index, struct journal_record *journal)
{
    int file_descriptor;
    int record_size;
    int stored_checksum;
    char filename[20];
    char header[JOURNAL_HEADER_SIZE];

    build_table_filename(table_index, filename, "JRN");
    file_descriptor = open(filename, O_RDONLY);
    if (file_descriptor == ERROR)
        return I_OK;
    if (read(file_descriptor, header, JOURNAL_HEADER_SIZE) == 0)
    {
        close(file_descriptor);
        return I_OK;
    }
    if (header[0] != 'D' || header[1] != 'X' ||
        header[2] != 'J' || header[3] != '1')
    {
        close(file_descriptor);
        return I_ERECOV;
    }
    record_size = read_little_endian_16(header + 6);
    if (record_size != g_cfg.tbls[table_index].recsz ||
        read(file_descriptor, journal->old_record, record_size) != record_size)
    {
        close(file_descriptor);
        return I_ERECOV;
    }
    close(file_descriptor);
    stored_checksum = read_little_endian_16(header + 22);
    header[22] = 0;
    header[23] = 0;
    if (stored_checksum != journal_checksum(header, journal->old_record,
        record_size))
        return I_ERECOV;
    journal->state = header[4] & 0xff;
    if (journal->state != JOURNAL_STATE_PREPARED &&
        journal->state != JOURNAL_STATE_COMMITTED)
        return I_ERECOV;
    journal->operation = header[5] & 0xff;
    journal->physical_slot = read_little_endian_16(header + 8);
    journal->old_record_exists = read_little_endian_16(header + 10);
    decode_metadata(header, 12, &journal->old_metadata);
    return journal->operation;
}

void i_test_crash_after(int phase)
{
    crash_after_phase = phase;
}

static int simulate_crash_after(int phase)
{
    if (crash_after_phase != phase)
        return 0;
    crash_after_phase = 0;
    return 1;
}

static int abort_journal(int table_index, int operation_error)
{
    int recovery_rc;

    recovery_rc = recover_table_journal(table_index);
    return recovery_rc == I_OK ? operation_error : recovery_rc;
}
#else
void i_test_crash_after(int phase)
{
    phase = phase;
}
#endif

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

    if (lseek(fd, 0L, 0) < 0L)
        return I_EINDEX;
    if (read(fd, hdr, PRIMARY_INDEX_HEADER_SIZE) != PRIMARY_INDEX_HEADER_SIZE)
        return I_EINDEX;
    if (hdr[0] != PRIMARY_INDEX_MAGIC_0 || hdr[1] != PRIMARY_INDEX_MAGIC_1 ||
        hdr[2] != PRIMARY_INDEX_MAGIC_2 || hdr[3] != PRIMARY_INDEX_MAGIC_3)
        return I_EINDEX;
    if (read_little_endian_16(hdr + 4) !=
            g_cfg.tbls[tidx].keyoff[key_index] ||
        read_little_endian_16(hdr + 6) !=
            g_cfg.tbls[tidx].keysz[key_index] ||
        read_little_endian_16(hdr + 8) != g_cfg.tbls[tidx].recsz)
        return I_EINDEX;
    if (read_little_endian_16(hdr + 20) != metadata_checksum(hdr))
        return I_EINDEX;
    metadata->index_count = read_little_endian_16(hdr + 10);
    metadata->active_count = read_little_endian_16(hdr + 12);
    metadata->high_water_slot = read_little_endian_16(hdr + 14);
    metadata->free_head = read_little_endian_16(hdr + 16);
    metadata->generation = read_little_endian_16(hdr + 18);
    if (metadata->free_head == NO_FREE_SLOT)
        metadata->free_head = -1;
    return I_OK;
}

static int write_index_header(int fd, int tidx, int key_index,
    const struct persistent_metadata *metadata)
{
    char hdr[PRIMARY_INDEX_HEADER_SIZE];

    memset(hdr, 0, PRIMARY_INDEX_HEADER_SIZE);
    hdr[0] = PRIMARY_INDEX_MAGIC_0;
    hdr[1] = PRIMARY_INDEX_MAGIC_1;
    hdr[2] = PRIMARY_INDEX_MAGIC_2;
    hdr[3] = PRIMARY_INDEX_MAGIC_3;
    write_little_endian_16(hdr + 4, g_cfg.tbls[tidx].keyoff[key_index]);
    write_little_endian_16(hdr + 6, g_cfg.tbls[tidx].keysz[key_index]);
    write_little_endian_16(hdr + 8, g_cfg.tbls[tidx].recsz);
    write_little_endian_16(hdr + 10, metadata->index_count);
    write_little_endian_16(hdr + 12, metadata->active_count);
    write_little_endian_16(hdr + 14, metadata->high_water_slot);
    write_little_endian_16(hdr + 16,
        metadata->free_head < 0 ? NO_FREE_SLOT : metadata->free_head);
    write_little_endian_16(hdr + 18, metadata->generation);
    write_little_endian_16(hdr + 20, metadata_checksum(hdr));
    if (lseek(fd, 0L, 0) < 0L)
        return I_EWRIT;
    if (write(fd, hdr, PRIMARY_INDEX_HEADER_SIZE) != PRIMARY_INDEX_HEADER_SIZE)
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
        g_cfg.tbls[table_index].idxsamp = 1;
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
    invalidate_table_files();
    if (rc == I_OK)
    {
        g_cfg.tbls[table_index].idxcnt = metadata->index_count;
        g_cfg.tbls[table_index].idxsamp = 1;
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
    if (lseek(file_descriptor, offset, 0) < 0L ||
        write(file_descriptor, link, 2) != 2)
        rc = I_EWRIT;
    if (rc == I_OK && fsync(file_descriptor) != 0)
        rc = I_EWRIT;
    close_table_file(file_descriptor);
    return rc;
}

int pki_create(const char *tblnam)
{
    int fd;
    int key_index;
    int rc;
    int tidx;
    char extension[4];
    char fname[20];
    struct persistent_metadata metadata;

    tidx = find_table_index(tblnam);
    if (tidx < 0)
        return tidx;
    invalidate_table_files();
    if (g_cfg.tbls[tidx].recsz < 3 ||
        g_cfg.tbls[tidx].nkeys < 1 || g_cfg.tbls[tidx].nkeys > I_MXKEY)
        return I_ESIZE;
    metadata.index_count = 0;
    metadata.active_count = 0;
    metadata.high_water_slot = 0;
    metadata.free_head = -1;
    metadata.generation = 0;
    rc = I_OK;
    for (key_index = 0; key_index < g_cfg.tbls[tidx].nkeys; key_index++)
    {
        if (g_cfg.tbls[tidx].keysz[key_index] < 1 ||
            g_cfg.tbls[tidx].keysz[key_index] > I_MXKEYLN ||
            g_cfg.tbls[tidx].keyoff[key_index] < 0 ||
            g_cfg.tbls[tidx].keyoff[key_index] +
                g_cfg.tbls[tidx].keysz[key_index] > g_cfg.tbls[tidx].recsz)
            return I_ESIZE;
        index_extension(key_index, extension);
        build_table_filename(tidx, fname, extension);
        fd = open(fname, O_CREAT | O_TRUNC | O_RDWR, 0);
        if (fd == ERROR)
            return I_EOPEN;
        rc = write_index_header(fd, tidx, key_index, &metadata);
        if (rc == I_OK && fsync(fd) != 0)
            rc = I_EWRIT;
        close(fd);
        if (rc != I_OK)
            return rc;
    }
    if (rc == I_OK)
    {
        build_table_filename(tidx, fname, "MAP");
        unlink(fname);
        g_cfg.tbls[tidx].idxcnt = 0;
        g_cfg.tbls[tidx].idxsamp = 1;
    }
    return rc;
}

int pki_load(const char *table_name)
{
    int file_descriptor;
    int key_index;
    int rc;
    int table_index;
    struct persistent_metadata metadata;
    struct persistent_metadata secondary_metadata;

    table_index = find_table_index(table_name);
    if (table_index < 0)
        return table_index;
    rc = recover_table_journal(table_index);
    if (rc != I_OK)
        return rc;
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
        if (rc != I_OK || secondary_metadata.index_count !=
            metadata.index_count)
            return pki_rebuild(table_name);
    }
    return I_OK;
}

static long index_entry_offset(int tidx, int key_index, int position)
{
    return (long)PRIMARY_INDEX_HEADER_SIZE +
        ((long)position * (g_cfg.tbls[tidx].keysz[key_index] + 2));
}

static int read_index_entry(int fd, int tidx, int key_index, int position,
    char *key, int *phys)
{
    int ksz;
    char ent[I_MXKEYLN + 2];

    ksz = g_cfg.tbls[tidx].keysz[key_index];
    if (lseek(fd, index_entry_offset(tidx, key_index, position), 0) < 0L)
        return I_EREAD;
    if (read(fd, ent, ksz + 2) != ksz + 2)
        return I_EREAD;
    if (key != 0)
        memcpy(key, ent, ksz);
    *phys = (ent[ksz] & 0xff) | ((ent[ksz + 1] & 0xff) << 8);
    return I_OK;
}

static int write_index_entry(int fd, int tidx, int key_index, int position,
    const char *key, int phys)
{
    int ksz;
    char ent[I_MXKEYLN + 2];

    ksz = g_cfg.tbls[tidx].keysz[key_index];
    memcpy(ent, key, ksz);
    ent[ksz] = phys & 0xff;
    ent[ksz + 1] = (phys >> 8) & 0xff;
    if (lseek(fd, index_entry_offset(tidx, key_index, position), 0) < 0L)
        return I_EWRIT;
    if (write(fd, ent, ksz + 2) != ksz + 2)
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
        if (lseek(file_descriptor,
            index_entry_offset(table_index, key_index, source_position), 0) < 0L ||
            read(file_descriptor, record_workspace, block_size) != block_size ||
            lseek(file_descriptor,
                index_entry_offset(table_index, key_index,
                    source_position + direction),
                0) < 0L ||
            write(file_descriptor, record_workspace, block_size) != block_size)
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
    const char *key, int *position, int *found)
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
        if (cmp < 0)
            lo = mid + 1;
        else
        {
            if (cmp == 0)
                *found = 1;
            hi = mid;
        }
    }
    *position = lo;
    return I_OK;
}

int idx_find(const char *tblnam, int key_index, const char *key, char *rec)
{
    int fd;
    int found;
    int phys;
    int position;
    int rc;
    int tidx;
    struct persistent_metadata metadata;

    stats_add(&i_runtime_stats.exact_lookups, 1);
    tidx = find_table_index(tblnam);
    if (tidx < 0)
        return tidx;
    if (key_index < 0 || key_index >= g_cfg.tbls[tidx].nkeys)
        return I_EINDEX;
    fd = open_index_file(tidx, key_index, O_RDONLY);
    if (fd == ERROR)
        return I_EINDEX;
    rc = read_index_header(fd, tidx, key_index, &metadata);
    if (rc == I_OK)
        rc = find_lower_bound(fd, tidx, key_index, metadata.index_count, key,
            &position, &found);
    if (rc == I_OK && !found)
        rc = I_ENREC;
    if (rc == I_OK)
        rc = read_index_entry(fd, tidx, key_index, position, 0, &phys);
    close_table_file(fd);
    if (rc != I_OK)
        return rc;
    if (phys == DELETED_INDEX_SLOT)
        return I_ENREC;
    if (rec != 0)
    {
        rc = i_rdphys(tblnam, rec, phys);
        if (rc != I_OK)
            return I_EINDEX;
    }
    return phys;
}

int key_find(const char *tblnam, const char *key, char *rec)
{
    return idx_find(tblnam, 0, key, rec);
}

static int remove_index_key(int tidx, int key_index, const char *key,
    int phys);

static int add_index_key(int tidx, int key_index, const char *rec, int phys)
{
    int fd;
    int found;
    int oldphys;
    int position;
    int rc;
    const char *key;
    struct persistent_metadata metadata;

    key = &rec[g_cfg.tbls[tidx].keyoff[key_index]];
    fd = open_index_file(tidx, key_index, O_RDWR);
    if (fd == ERROR)
        return I_EINDEX;
    rc = read_index_header(fd, tidx, key_index, &metadata);
    if (rc == I_OK)
        rc = find_lower_bound(fd, tidx, key_index, metadata.index_count, key,
            &position, &found);
    if (rc == I_OK && found)
    {
        rc = read_index_entry(fd, tidx, key_index, position, 0, &oldphys);
        if (rc == I_OK && oldphys != DELETED_INDEX_SLOT)
            rc = I_EDUP;
        if (rc == I_OK)
            rc = write_index_entry(fd, tidx, key_index, position, key, phys);
    }
    if (rc == I_OK && !found && position < metadata.index_count)
        rc = move_index_entries(fd, tidx, key_index, position,
            metadata.index_count - position, 1);
    if (rc == I_OK && !found)
        rc = write_index_entry(fd, tidx, key_index, position, key, phys);
    if (rc == I_OK && !found)
    {
        metadata.index_count++;
        rc = write_index_header(fd, tidx, key_index, &metadata);
    }
    if (rc == I_OK && fsync(fd) != 0)
        rc = I_EWRIT;
    if (!defer_table_file_invalidation)
        invalidate_table_files();
    if (rc == I_OK && key_index == 0)
        g_cfg.tbls[tidx].idxcnt = metadata.index_count;
    return rc;
}

int key_add(const char *tblnam, const char *rec, int phys)
{
    const char *saved_record;
    int key_index;
    int rc;
    int tidx;

    tidx = find_table_index(tblnam);
    if (tidx < 0)
        return tidx;
    if (rec == record_workspace)
    {
        memcpy(key_record_workspace, rec, g_cfg.tbls[tidx].recsz);
        saved_record = key_record_workspace;
    }
    else
        saved_record = rec;
    for (key_index = 0; key_index < g_cfg.tbls[tidx].nkeys; key_index++)
    {
        rc = add_index_key(tidx, key_index, saved_record, phys);
        if (rc != I_OK)
        {
            while (--key_index >= 0)
                remove_index_key(tidx, key_index,
                    &saved_record[g_cfg.tbls[tidx].keyoff[key_index]], phys);
            return rc;
        }
    }
    return I_OK;
}

static int tombstone_index_key(int tidx, int key_index, const char *key,
    int phys)
{
    int entry_phys;
    int file_descriptor;
    int found;
    int position;
    int rc;
    struct persistent_metadata metadata;

    file_descriptor = open_index_file(tidx, key_index, O_RDWR);
    if (file_descriptor == ERROR)
        return I_EINDEX;
    rc = read_index_header(file_descriptor, tidx, key_index, &metadata);
    if (rc == I_OK)
        rc = find_lower_bound(file_descriptor, tidx, key_index,
            metadata.index_count,
            key, &position, &found);
    if (rc == I_OK && !found)
        rc = I_EINDEX;
    if (rc == I_OK)
        rc = read_index_entry(file_descriptor, tidx, key_index, position, 0,
            &entry_phys);
    if (rc == I_OK && entry_phys != phys)
        rc = I_EINDEX;
    if (rc == I_OK)
        rc = write_index_entry(file_descriptor, tidx, key_index, position, key,
            DELETED_INDEX_SLOT);
    if (rc == I_OK && fsync(file_descriptor) != 0)
        rc = I_EWRIT;
    if (!defer_table_file_invalidation)
        invalidate_table_files();
    return rc;
}

static int remove_index_key(int tidx, int key_index, const char *key, int phys)
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
            &position, &found);
    if (rc == I_OK && !found)
        rc = I_EINDEX;
    if (rc == I_OK)
        rc = read_index_entry(fd, tidx, key_index, position, 0, &entphys);
    if (rc == I_OK && entphys != phys)
        rc = I_EINDEX;
    if (rc == I_OK && position < metadata.index_count - 1)
        rc = move_index_entries(fd, tidx, key_index, position + 1,
            metadata.index_count - position - 1, -1);
    if (rc == I_OK)
    {
        metadata.index_count--;
        rc = write_index_header(fd, tidx, key_index, &metadata);
    }
    if (rc == I_OK && fsync(fd) != 0)
        rc = I_EWRIT;
    if (!defer_table_file_invalidation)
        invalidate_table_files();
    if (rc == I_OK && key_index == 0)
        g_cfg.tbls[tidx].idxcnt = metadata.index_count;
    return rc;
}

int key_remove(const char *tblnam, const char *rec, int phys)
{
    int key_index;
    int rc;
    int tidx;

    tidx = find_table_index(tblnam);
    if (tidx < 0)
        return tidx;
    for (key_index = 0; key_index < g_cfg.tbls[tidx].nkeys; key_index++)
    {
        rc = remove_index_key(tidx, key_index,
            &rec[g_cfg.tbls[tidx].keyoff[key_index]], phys);
        if (rc != I_OK)
            return rc;
    }
    return I_OK;
}

static int key_tombstone(int tidx, const char *rec, int phys)
{
    int key_index;
    int rc;

    for (key_index = 0; key_index < g_cfg.tbls[tidx].nkeys; key_index++)
    {
        rc = tombstone_index_key(tidx, key_index,
            &rec[g_cfg.tbls[tidx].keyoff[key_index]], phys);
        if (rc != I_OK)
            return rc;
    }
    return I_OK;
}

int key_update(const char *tblnam, const char *oldrec,
    const char *newrec, int phys)
{
    const char *saved_old_record;
    int key_index;
    int ksz;
    int koff;
    int rc;
    int tidx;

    tidx = find_table_index(tblnam);
    if (tidx < 0)
        return tidx;
    memcpy(key_record_workspace, oldrec, g_cfg.tbls[tidx].recsz);
    saved_old_record = key_record_workspace;
    for (key_index = 0; key_index < g_cfg.tbls[tidx].nkeys; key_index++)
    {
        koff = g_cfg.tbls[tidx].keyoff[key_index];
        ksz = g_cfg.tbls[tidx].keysz[key_index];
        if (compare_keys(&saved_old_record[koff], &newrec[koff], ksz) != 0)
        {
            rc = idx_find(tblnam, key_index, &newrec[koff], 0);
            if (rc >= 0)
                return I_EDUP;
            if (rc != I_ENREC)
                return rc;
            rc = remove_index_key(tidx, key_index,
                &saved_old_record[koff], phys);
            if (rc != I_OK)
                return rc;
            rc = add_index_key(tidx, key_index, newrec, phys);
            if (rc != I_OK)
            {
                add_index_key(tidx, key_index, saved_old_record, phys);
                return rc;
            }
        }
    }
    return I_OK;
}

static int read_cursor_record(const char *tblnam, struct i_cursor *cursor,
    char *rec)
{
    int fd;
    int phys;
    int rc;

    char key[I_MXKEYLN];

    fd = open_index_file(cursor->table_index, cursor->key_index, O_RDONLY);
    if (fd == ERROR)
        return I_EINDEX;
    rc = I_OK;
    phys = DELETED_INDEX_SLOT;
    while (cursor->position >= 0 &&
        cursor->position < cursor->entry_count &&
        phys == DELETED_INDEX_SLOT)
    {
        rc = read_index_entry(fd, cursor->table_index, cursor->key_index,
            cursor->position, key, &phys);
        if (rc != I_OK || phys != DELETED_INDEX_SLOT)
            break;
        cursor->position++;
    }
    close_table_file(fd);
    if (rc != I_OK)
        return rc;
    if (phys == DELETED_INDEX_SLOT)
        return I_ENREC;
    if (cursor->prefix_length > 0 && compare_keys(key, cursor->prefix,
        cursor->prefix_length) != 0)
        return I_ENREC;
    rc = i_rdphys(tblnam, rec, phys);
    if (rc != I_OK)
        return I_EINDEX;
    stats_add(&i_runtime_stats.cursor_rows, 1);
    return phys;
}

int idx_open(const char *tblnam, int key_index, const char *prefix,
    int prefix_length, struct i_cursor *cursor, char *rec)
{
    int fd;
    int found;
    int rc;
    int tidx;
    char lower_key[I_MXKEYLN];
    struct persistent_metadata metadata;

    stats_add(&i_runtime_stats.cursor_starts, 1);
    tidx = find_table_index(tblnam);
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
            lower_key, &cursor->position, &found);
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

int cur_first(const char *tblnam, struct i_cursor *cursor, char *rec)
{
    return idx_open(tblnam, 0, 0, 0, cursor, rec);
}

int cur_next(const char *tblnam, struct i_cursor *cursor, char *rec)
{
    cursor->position++;
    return read_cursor_record(tblnam, cursor, rec);
}

static int rebuild_table_state(const char *table_name, int table_index,
    int high_water_slot, int generation)
{
    int active_count;
    int free_head;
    int physical_slot;
    int rc;
    char *record;
    struct persistent_metadata metadata;

    record = record_workspace;
    rc = pki_create(table_name);
    if (rc != I_OK)
        return rc;
    active_count = 0;
    free_head = -1;
    g_cfg.tbls[table_index].maxrec = high_water_slot;
    for (physical_slot = 0; physical_slot < high_water_slot; physical_slot++)
    {
        rc = i_rdphys(table_name, record, physical_slot);
        if (rc == I_ENREC)
        {
            rc = rebuild_free_link(table_index, physical_slot, free_head);
            if (rc != I_OK)
                return rc;
            free_head = physical_slot;
            continue;
        }
        if (rc != I_OK)
            return rc;
        rc = key_add(table_name, record, physical_slot);
        if (rc != I_OK)
            return rc;
        active_count++;
    }
    metadata.index_count = active_count;
    metadata.active_count = active_count;
    metadata.high_water_slot = high_water_slot;
    metadata.free_head = free_head;
    metadata.generation = generation;
    return save_table_metadata(table_index, &metadata);
}

#ifndef DXISAM_DISABLE_JOURNAL
static int recover_table_journal(int table_index)
{
    int data_file;
    int operation;
    int rc;
    long offset;

    invalidate_table_files();
    operation = read_journal(table_index, &active_journal);
    if (operation == I_OK)
        return I_OK;
    if (operation < 0)
        return operation;
    if (active_journal.state == JOURNAL_STATE_COMMITTED)
        return clear_journal(table_index);
    data_file = open_table_file(table_index, "DAT", O_RDWR);
    if (data_file == ERROR)
        return I_ERECOV;
    rc = I_OK;
    offset = (long)active_journal.physical_slot *
        g_cfg.tbls[table_index].recsz;
    if (active_journal.old_record_exists)
    {
        if (lseek(data_file, offset, 0) < 0L ||
            write(data_file, active_journal.old_record,
                g_cfg.tbls[table_index].recsz) !=
                    g_cfg.tbls[table_index].recsz ||
            fsync(data_file) != 0)
            rc = I_ERECOV;
    }
    close_table_file(data_file);
    if (rc != I_OK)
        return rc;
    rc = rebuild_table_state(g_cfg.tbls[table_index].name, table_index,
        active_journal.old_metadata.high_water_slot,
        active_journal.old_metadata.generation);
    if (rc != I_OK)
        return I_ERECOV;
    return clear_journal(table_index);
}
#endif

int pki_rebuild(const char *tblnam)
{
    int rc;
    int tidx;
    int high_water_slot;
    int generation;
    struct persistent_metadata metadata;

    invalidate_table_files();
    tidx = find_table_index(tblnam);
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
    rc = rebuild_table_state(tblnam, tidx, high_water_slot, generation);
    if (rc != I_OK)
        return rc;
    return clear_journal(tidx);
}

static int read_data_slot(int file_descriptor, int record_size,
    int physical_slot, char *record)
{
    long offset;

    offset = (long)physical_slot * record_size;
    if (lseek(file_descriptor, offset, 0) < 0L)
        return I_EREAD;
    if (read(file_descriptor, record, record_size) != record_size)
        return I_EREAD;
    return I_OK;
}

int table_verify(const char *table_name, struct i_verify *report)
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
    char *record;
    struct persistent_metadata metadata;
    struct persistent_metadata index_metadata;

    record = record_workspace;
    report->index_entries = 0;
    report->active_records = 0;
    report->deleted_records = 0;
    report->free_slots = 0;
    report->errors = 0;
    table_index = find_table_index(table_name);
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
                    record[0] == I_DELFLAG ||
                    compare_keys(key,
                        &record[g_cfg.tbls[table_index].keyoff[key_index]],
                        g_cfg.tbls[table_index].keysz[key_index]) != 0)
                    report->errors++;
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
    if (report->free_slots > metadata.high_water_slot)
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