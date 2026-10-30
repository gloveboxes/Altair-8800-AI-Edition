#include "stdlib.h"

#define SORT_BYTES 2048
static int building_table = -1;
static int sort_width;

static int build_close(int fd, int rc)
{
    if (fd >= 0 && engine_close(fd) != 0 && rc == I_OK)
        return I_EWRIT;
    return rc;
}

static int build_pending(int table)
{
    char name[20];
    int fd;
    build_table_filename(table, name, "BLD");
    fd = open(name, O_RDONLY);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

static int mark_build(int table)
{
    char name[20];
    int fd, rc;
    build_table_filename(table, name, "BLD");
    fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, 0);
    if (fd < 0)
        return I_EOPEN;
    rc = write(fd, "BUILD", 5) == 5 && fsync(fd) == 0 ? I_OK : I_EWRIT;
    if (close(fd) != 0 && rc == I_OK)
        rc = I_EWRIT;
    return rc;
}

static void sort_run(char *data, int count)
{
    int gap, i, j;
    char entry[I_MXKEYLN + 2];
    for (gap = count / 2; gap; gap /= 2)
        for (i = gap; i < count; ++i)
        {
            memcpy(entry, data + i * sort_width, sort_width);
            j = i;
            while (j >= gap &&
                memcmp(data + (j - gap) * sort_width, entry, sort_width - 2) > 0)
            {
                memcpy(data + j * sort_width,
                    data + (j - gap) * sort_width, sort_width);
                j -= gap;
            }
            memcpy(data + j * sort_width, entry, sort_width);
        }
}

struct merge_input {
    int fd, left, used, bytes;
    char *buffer;
};

static int merge_next(struct merge_input *input)
{
    int take;
    if (!input->left)
        return I_ENREC;
    if (input->used == input->bytes)
    {
        take = 512 / sort_width;
        if (take > input->left)
            take = input->left;
        input->bytes = take * sort_width;
        input->used = 0;
        if (read(input->fd, input->buffer, input->bytes) != input->bytes)
            return I_EREAD;
    }
    return I_OK;
}

static int merge_runs(int source, int second, int dest, int total,
    int run, char *scratch)
{
    struct merge_input left, right, *take;
    int base, a, b, out, rc;
    left.fd = source;
    right.fd = second;
    left.buffer = scratch;
    right.buffer = scratch + 512;
    base = 0;
    out = 0;
    while (base < total)
    {
        a = total - base;
        if (a > run) a = run;
        b = total - base - a;
        if (b > run) b = run;
        if (lseek(source, (long)base * sort_width, 0) < 0L ||
            lseek(second, (long)(base + a) * sort_width, 0) < 0L)
            return I_EREAD;
        left.left = a;
        right.left = b;
        left.used = left.bytes = right.used = right.bytes = 0;
        while (left.left || right.left)
        {
            rc = merge_next(&left);
            if (rc != I_OK && rc != I_ENREC) return rc;
            rc = merge_next(&right);
            if (rc != I_OK && rc != I_ENREC) return rc;
            take = &left;
            if (!left.left || (right.left &&
                memcmp(left.buffer + left.used,
                    right.buffer + right.used, sort_width - 2) > 0))
                take = &right;
            if (out + sort_width > 1024)
            {
                if (write(dest, scratch + 1024, out) != out)
                    return I_EWRIT;
                out = 0;
            }
            memcpy(scratch + 1024 + out, take->buffer + take->used, sort_width);
            out += sort_width;
            take->used += sort_width;
            take->left--;
        }
        base += a + b;
    }
    return out && write(dest, scratch + 1024, out) != out ? I_EWRIT : I_OK;
}

static int sorted_index(int table, int key, struct persistent_metadata *meta,
    char *scratch)
{
    static char names[2][20], data_name[20], index_name[20], extension[4];
    static char previous[I_MXKEYLN];
    int data, output, input, second, count, slot, rc, total, run, which, bytes, i;
    build_table_filename(table, names[0], "RN0");
    build_table_filename(table, names[1], "RN1");
    build_table_filename(table, data_name, "DAT");
    index_extension(key, extension);
    build_table_filename(table, index_name, extension);
    output = open(index_name, O_CREAT | O_TRUNC | O_RDWR, 0);
    rc = build_close(output, output < 0 ? I_EOPEN : I_OK);
    if (rc != I_OK)
        return rc;
    sort_width = g_cfg.tbls[table].keysz[key] + 2;
    run = SORT_BYTES / sort_width;
    data = open(data_name, O_RDONLY);
    output = open(names[0], O_CREAT | O_TRUNC | O_RDWR, 0);
    rc = data < 0 || output < 0 ? I_EOPEN : I_OK;
    count = total = 0;
    for (slot = 0; rc == I_OK && slot < meta->high_water_slot; ++slot)
    {
        bytes = g_cfg.tbls[table].recsz;
        if (read(data, record_workspace, bytes) != bytes)
        {
            rc = I_EREAD;
            break;
        }
        if (record_workspace[0] == I_DELFLAG)
            continue;
        make_index_key(table, key, record_workspace, scratch + count * sort_width);
        write_little_endian_16(scratch + count * sort_width + sort_width - 2, slot);
        ++count;
        ++total;
        if (count == run)
        {
            sort_run(scratch, count);
            bytes = count * sort_width;
            if (write(output, scratch, bytes) != bytes) rc = I_EWRIT;
            count = 0;
        }
    }
    if (rc == I_OK && count)
    {
        sort_run(scratch, count);
        bytes = count * sort_width;
        if (write(output, scratch, bytes) != bytes) rc = I_EWRIT;
    }
    rc = build_close(data, rc);
    rc = build_close(output, rc);
    which = 0;
    while (rc == I_OK && run < total)
    {
        input = open(names[which], O_RDONLY);
        second = open(names[which], O_RDONLY);
        output = open(names[1 - which], O_CREAT | O_TRUNC | O_RDWR, 0);
        if (input < 0 || second < 0 || output < 0)
            rc = I_EOPEN;
        else
            rc = merge_runs(input, second, output, total, run, scratch);
        rc = build_close(input, rc);
        rc = build_close(second, rc);
        rc = build_close(output, rc);
        which = 1 - which;
        run = run > total / 2 ? total : run * 2;
    }
    if (rc == I_OK && total > SORT_BYTES / sort_width &&
        unlink(names[1 - which]) != 0)
        rc = I_EWRIT;
    if (rc == I_OK)
    {
        input = open(names[which], O_RDONLY);
        output = open(index_name, O_RDWR);
        if (input < 0 || output < 0)
            rc = I_EOPEN;
        else
            rc = write_index_header(output, table, key, meta);
        count = 0;
        while (rc == I_OK && count < total)
        {
            bytes = total - count;
            if (bytes > SORT_BYTES / sort_width) bytes = SORT_BYTES / sort_width;
            bytes *= sort_width;
            if (read(input, scratch, bytes) != bytes)
            {
                rc = I_EREAD;
                break;
            }
            for (i = 0; i < bytes; i += sort_width)
            {
                if (count && memcmp(previous, scratch + i, sort_width - 2) >= 0)
                {
                    rc = I_EDUP;
                    break;
                }
                memcpy(previous, scratch + i, sort_width - 2);
                ++count;
            }
            if (rc == I_OK && engine_write(output, scratch, bytes) != bytes)
                rc = I_EWRIT;
        }
        if (output >= 0 && rc == I_OK && fsync(output) != 0) rc = I_EWRIT;
        rc = build_close(input, rc);
        rc = build_close(output, rc);
    }
    if (unlink(names[which]) != 0 && rc == I_OK) rc = I_EWRIT;
    if (rc != I_OK && total > SORT_BYTES / sort_width)
        unlink(names[1 - which]);
    return rc;
}

static int build_indexes(int table, int high_water, int generation)
{
    char *scratch;
    char name[20];
    int fd, slot, size, key, rc;
    struct persistent_metadata meta;
    if (high_water < 0)
        return I_EINDEX;
    invalidate_table_files();
#ifdef ISAM_BANK_LIBRARY
    scratch = page_data;
    page_data = 0;
#else
    scratch = malloc(SORT_BYTES);
#endif
    if (!scratch)
        return I_EBANK;
    rc = mark_build(table);
    building_table = table;
    meta.index_count = meta.active_count = 0;
    meta.free_head = -1;
    meta.high_water_slot = high_water;
    meta.generation = generation;
    build_table_filename(table, name, "DAT");
    fd = open(name, O_RDWR);
    if (fd < 0) rc = I_EOPEN;
    size = g_cfg.tbls[table].recsz;
    for (slot = 0; rc == I_OK && slot < high_water; ++slot)
    {
        if (read(fd, record_workspace, size) != size)
            rc = I_EREAD;
        else if (record_workspace[0] == I_DELFLAG)
        {
            set_free_link(record_workspace, meta.free_head);
            if (lseek(fd, (long)slot * size, 0) < 0L ||
                write(fd, record_workspace, size) != size)
                rc = I_EWRIT;
            meta.free_head = slot;
        }
        else
            ++meta.active_count;
    }
    if (fd >= 0 && rc == I_OK && fsync(fd) != 0) rc = I_EWRIT;
    rc = build_close(fd, rc);
    meta.index_count = meta.active_count;
    for (key = 0; rc == I_OK && key < g_cfg.tbls[table].nkeys; ++key)
        rc = sorted_index(table, key, &meta, scratch);
    invalidate_table_files();
    if (rc == I_OK)
    {
        build_table_filename(table, name, "BLD");
        if (unlink(name) != 0)
            rc = I_EWRIT;
        else
        {
            g_cfg.tbls[table].nrecs = meta.active_count;
            g_cfg.tbls[table].idxcnt = meta.index_count;
            g_cfg.tbls[table].maxrec = high_water;
            building_table = -1;
        }
    }
#ifdef ISAM_BANK_LIBRARY
    page_data = scratch;
    discard_pages(-1);
#else
    free(scratch);
#endif
    return rc;
}

static int begin_build(int table)
{
    int rc;
    if (building_table >= 0 || g_cfg.tbls[table].nrecs || g_cfg.tbls[table].maxrec)
        return I_EBUILD;
    invalidate_table_files();
    rc = mark_build(table);
    if (rc == I_OK)
        building_table = table;
    return rc;
}

static int append_build(int table, const char *records, int bytes)
{
    int fd, size, count, i, rc;
    if (building_table != table)
        return I_EBUILD;
    size = g_cfg.tbls[table].recsz;
    if (bytes < 0 || bytes % size)
        return I_ESIZE;
    count = bytes / size;
    if (count > 32767 - g_cfg.tbls[table].maxrec)
        return I_EFULL;
    for (i = 0; i < bytes; i += size)
        if (records[i] == I_DELFLAG)
            return I_ERECORD;
    if (!bytes)
        return I_OK;
    fd = open_table_file(table, "DAT", O_RDWR);
    if (fd < 0)
        return I_EOPEN;
    rc = I_OK;
    if (engine_seek(fd, (long)g_cfg.tbls[table].maxrec * size, 0) < 0L ||
        engine_write(fd, records, bytes) != bytes || fsync(fd) != 0)
        rc = I_EWRIT;
    close_table_file(fd);
    if (rc == I_OK)
        g_cfg.tbls[table].maxrec += count;
    return rc;
}
