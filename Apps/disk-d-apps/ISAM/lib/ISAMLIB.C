#define ISAM_BANK_LIBRARY
#include "../isamdb.c"
#undef close
#include "../isam.h"
#include "../../../disk-b-apps/BANKSDK/BIOSBANK.H"

#pragma dcc bank_buffer iscall 4 inout 5 128
#pragma dcc bank_buffer ispost 4 in 5 128
#pragma dcc bank_record iswalk 1 inout
#pragma dcc bank_buffer iswalk 2 out 3 128

static unsigned int bulk_address;
static int defining;
static struct {
    char *source[DB_FIELDS];
    int sizes[DB_FIELDS], count, width;
} projection;
typedef char table_tail_size[(sizeof(struct i_tbl) == sizeof(struct db_info)) ? 1 : -1];

unsigned int onload(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    istat_reset();
    bulk_address = 0;
    defining = 0;
    building_table = -1;
    cached_close_error = 0;
    invalidate_table_files();
    if (start_pages() != I_OK)
    {
        puts("ISAM cannot allocate bank-1 page cache.");
        return 0;
    }
    return 1;
}

unsigned int onunload(void)
{
    if (building_table >= 0)
        puts("ISAM build incomplete; table remains unavailable.");
    invalidate_table_files();
    stop_pages();
    bulk_address = 0;
    if (cached_close_error)
    {
        puts("ISAM file close failed.");
        return 0;
    }
    return 1;
}

static int table_valid(int tab)
{
    return tab >= 0 && tab < g_cfg.ntbls && tab < I_MXTBL;
}

static int key_valid(int tab, int key)
{
    return table_valid(tab) && key >= 0 && key < g_cfg.tbls[tab].nkeys;
}

static int open_database(char *filename)
{
    int rc, table, key, fd;
    invalidate_table_files();
    rc = i_cfrd(filename);
    if (rc != I_OK)
        return rc;
    for (table = 0; table < g_cfg.ntbls; ++table)
    {
        fd = open_table_file(table, "DAT", O_RDWR);
        if (fd == ERROR)
            return I_EOPEN;
        close_table_file(fd);
        for (key = 0; key < g_cfg.tbls[table].nkeys; ++key)
        {
            fd = open_index_file(table, key, O_RDWR);
            if (fd == ERROR)
                return I_EOPEN;
            close_table_file(fd);
        }
    }
    return cached_close_error ? cached_close_error : I_OK;
}

static int define_database(const struct db_schema *schema)
{
    int table, rc;
    if (g_cfg.ntbls || schema->tables < 1 || schema->tables > I_MXTBL)
        return I_ESIZE;
    memset(&g_cfg, 0, sizeof(g_cfg));
    memcpy(g_cfg.dbname, schema->name, I_MXNM);
    g_cfg.ntbls = schema->tables;
    for (table = 0; table < schema->tables; ++table)
        memcpy(&g_cfg.tbls[table], &schema->table[table], sizeof(struct db_table));
    rc = valid_config(&g_cfg);
    if (rc != I_OK)
    {
        memset(&g_cfg, 0, sizeof(g_cfg));
        return rc;
    }
    defining = 1;
    return I_OK;
}

static int prepare_view(int table, const struct db_view *view)
{
    int field, offset, size, total, record_size;
    record_size = g_cfg.tbls[table].recsz;
    projection.count = 0;
    projection.width = record_size;
    if (!view->fields)
        return I_OK;
    if (view->fields > DB_FIELDS)
        return I_ESIZE;
    total = 0;
    for (field = 0; field < view->fields; ++field)
    {
        offset = view->offsets[field];
        size = view->sizes[field];
        if (size < 1 || size > record_size || offset > record_size - size ||
            total > I_RECSZ - size)
            return I_ESIZE;
        total += size;
        projection.source[field] = record_workspace + offset;
        projection.sizes[field] = size;
    }
    projection.count = view->fields;
    projection.width = total;
    return I_OK;
}

static void pack_view(char *output)
{
    int field, size;
    for (field = 0; field < projection.count; ++field)
    {
        size = projection.sizes[field];
        memcpy(output, projection.source[field], size);
        output += size;
    }
}

static int cursor_valid(struct i_cursor *cursor)
{
    return key_valid(cursor->table_index, cursor->key_index) &&
        cursor->position >= 0 && cursor->entry_count >= 0 &&
        cursor->position <= cursor->entry_count &&
        cursor->prefix_length >= 0 &&
        cursor->prefix_length <=
            g_cfg.tbls[cursor->table_index].keysz[cursor->key_index];
}

int iswalk(struct i_cursor *cursor, unsigned char *data, unsigned int bytes)
{
    int name;
    if (building_table >= 0)
        return I_EBUILD;
    if (!key_valid(cursor->table_index, cursor->key_index))
        return I_EINDEX;
    if (bytes && bytes != g_cfg.tbls[cursor->table_index].recsz)
        return I_ESIZE;
    name = cursor->table_index;
    if (cursor->entry_count == -1)
        return idx_open(name, cursor->key_index, cursor->prefix,
            cursor->prefix_length, cursor, bytes ? (char *)data : 0);
    if (!cursor_valid(cursor))
        return I_EINDEX;
    return cursor->position >= cursor->entry_count ? I_ENREC :
        cur_next(name, cursor, bytes ? (char *)data : 0);
}

static int scan_rows(int operation, struct db_scan *scan)
{
    char *record, *rows;
    int wanted, slot, rc, name, fd, data_fd;
    wanted = scan->bytes;
    scan->bytes = scan->count = scan->width = 0;
    if (!bulk_address || !key_valid(scan->cursor.table_index,
        scan->cursor.key_index))
        return I_EINDEX;
    if (scan->mode == DB_RECORDS)
    {
        rc = prepare_view(scan->cursor.table_index, &scan->view);
        if (rc != I_OK)
            return rc;
        scan->width = projection.width;
    }
    else if (scan->mode == DB_KEYS)
        scan->width = g_cfg.tbls[scan->cursor.table_index].keysz[
            scan->cursor.key_index] + 2;
    else if (scan->mode == DB_SLOTS)
        scan->width = 2;
    else
        return I_ESIZE;
    if (wanted < scan->width || wanted > DB_BULK_BYTES)
        return I_ESIZE;
    if (operation == DB_SCAN)
        scan->cursor.entry_count = -1;
    else if (!cursor_valid(&scan->cursor))
        return I_EINDEX;
    wanted /= scan->width;
    rows = (char *)bulk_address;
    name = scan->cursor.table_index;
    if (operation == DB_SCAN)
    {
        slot = idx_open(name, scan->cursor.key_index, scan->cursor.prefix,
            scan->cursor.prefix_length, &scan->cursor, 0);
        if (slot < 0)
            return slot;
    }
    fd = open_index_file(name, scan->cursor.key_index, O_RDONLY);
    if (fd == ERROR)
        return I_EINDEX;
    if (operation != DB_SCAN)
    {
        if (operation == DB_NEXT && scan->cursor.position < scan->cursor.entry_count)
            ++scan->cursor.position;
        slot = read_cursor_slot(fd, &scan->cursor, scan->cursor.position);
    }
    data_fd = -1;
    if (scan->mode == DB_RECORDS)
    {
        data_fd = open_table_file(name, "DAT", O_RDONLY);
        if (data_fd == ERROR)
        {
            close_table_file(fd);
            return I_EOPEN;
        }
    }
    rc = I_OK;
    while (scan->count < wanted)
    {
        if (scan->count)
        {
            if (scan->cursor.position < scan->cursor.entry_count)
                ++scan->cursor.position;
            slot = read_cursor_slot(fd, &scan->cursor, -1);
        }
        if (slot < 0)
        {
            rc = slot;
            break;
        }
        record = rows + scan->bytes;
        if (scan->mode == DB_RECORDS)
        {
            rc = read_record_slot(name, data_fd,
                projection.count ? record_workspace : record, slot);
            if (rc != I_OK)
            {
                rc = I_EINDEX;
                break;
            }
            if (projection.count)
                pack_view(record);
        }
        else
        {
            if (scan->mode == DB_KEYS)
                memcpy(record, cursor_key, scan->width - 2);
            write_little_endian_16(record + scan->width - 2, slot);
        }
        scan->count++;
        scan->bytes += scan->width;
    }
    close_table_file(fd);
    if (data_fd >= 0)
        close_table_file(data_fd);
    return rc;
}

static int pick_rows(int table, struct db_pick *request)
{
    struct persistent_metadata metadata;
    char *input, *output, *record;
    int width, wanted, count, input_width, rc, fd, data_fd, slot, order;
    wanted = request->count;
    input_width = request->width;
    if (request->index == DB_PHYSICAL)
        width = sizeof(int);
    else
    {
        if (!key_valid(table, request->index))
            return I_EINDEX;
        width = g_cfg.tbls[table].keysz[request->index];
    }
    if (input_width != width)
        return I_ESIZE;
    rc = prepare_view(table, &request->view);
    if (rc != I_OK)
        return rc;
    width = projection.width;
    if (!bulk_address || wanted < 1 ||
        wanted > DB_BULK_BYTES / (input_width + width) ||
        request->bytes < 1 || request->bytes > DB_BULK_BYTES ||
        wanted > request->bytes / width)
        return I_ESIZE;
    input = (char *)bulk_address + DB_BULK_BYTES - wanted * input_width;
    output = (char *)bulk_address;
    fd = -1;
    if (request->index != DB_PHYSICAL)
    {
        fd = open_index_file(table, request->index, O_RDONLY);
        if (fd == ERROR)
            return I_EINDEX;
        rc = read_index_header(fd, table, request->index, &metadata);
        if (rc != I_OK)
        {
            close_table_file(fd);
            return rc;
        }
    }
    data_fd = open_table_file(table, "DAT", O_RDONLY);
    if (data_fd == ERROR)
    {
        if (fd >= 0) close_table_file(fd);
        return I_EOPEN;
    }
    lookup_position = -1;
    for (count = 0; count < wanted; ++count)
    {
        record = projection.count ? record_workspace : output;
        if (request->index == DB_PHYSICAL)
            slot = read_little_endian_16(input);
        else
        {
            if (count)
            {
                order = compare_keys(input, input - input_width, input_width);
                if (order < 0)
                    lookup_position = -1;
                else if (order > 0)
                    ++lookup_position;
            }
            slot = lookup_slot(table, request->index, fd, metadata.index_count,
                input);
        }
        if (slot < 0)
            rc = request->index == DB_PHYSICAL ? I_ENREC : slot;
        else
        {
            rc = read_record_slot(table, data_fd, record, slot);
            if (rc != I_OK && request->index != DB_PHYSICAL)
                rc = I_EINDEX;
        }
        if (rc != I_OK)
            break;
        if (projection.count)
            pack_view(output);
        output += width;
        input += input_width;
    }
    if (fd >= 0) close_table_file(fd);
    close_table_file(data_fd);
    if (rc != I_OK)
        return rc;
    request->bytes = wanted * width;
    request->width = width;
    return I_OK;
}

int ispost(unsigned int operation, int table, int argument,
    const unsigned char *data, unsigned int bytes)
{
    int name;
    if (operation == DB_DEFINE)
        return bytes == sizeof(struct db_schema) ?
            define_database((const struct db_schema *)data) : I_ESIZE;
    if (!table_valid(table))
        return I_ENTBL;
    name = table;
    if (building_table >= 0)
        return I_EBUILD;
    if (operation == DB_PROBE)
    {
        if (!key_valid(table, argument))
            return I_EINDEX;
        return bytes == g_cfg.tbls[table].keysz[argument] ?
            idx_find(name, argument, (const char *)data, 0) : I_ESIZE;
    }
    if (bytes != g_cfg.tbls[table].recsz)
        return I_ESIZE;
    if (operation == DB_ADD)
        return i_insrt(name, (char *)data, bytes);
    if (operation == DB_PUT)
        return i_wrphys(name, (char *)data, bytes, argument);
    return I_EBANK;
}

int iscall(unsigned int operation, int table, int argument,
    unsigned char *data, unsigned int bytes)
{
    struct db_info *info;
    unsigned int address, frame;
    int name, rc, i, wanted, width, *completed;
    switch (operation)
    {
    case DB_OPEN:
    case DB_CREATE:
    case DB_TABLE:
        if (!bytes || bytes > I_MXNM || !data[0] || data[bytes - 1])
            return I_ESIZE;
        if (operation == DB_OPEN)
            return open_database((char *)data);
        if (operation == DB_TABLE)
            return find_table_index((char *)data);
        rc = valid_config(&g_cfg);
        if (!defining)
            return I_ESIZE;
        for (i = 0; rc == I_OK && i < g_cfg.ntbls; ++i)
            rc = i_mktbl(i);
        if (rc == I_OK)
            rc = i_cfwr((char *)data);
        defining = 0;
        return rc;
    case DB_RESET:
        if (bytes) return I_ESIZE;
        istat_reset();
        return I_OK;
    case DB_STATS:
        if (bytes != sizeof(struct i_stats)) return I_ESIZE;
        iget_stats((struct i_stats *)data);
        return I_OK;
    case DB_BIND:
        if (bytes != sizeof(int) || *(int *)data != DB_BULK_BYTES)
            return I_ESIZE;
        address = (unsigned int)argument;
        frame = (unsigned int)data & 0xff00U;
        if (address < BMBIOS_HEAP_START || (address & 255) ||
            address > BMBIOS_HEAP_START + BMBIOS_HEAP_BYTES - DB_BULK_BYTES ||
            (address < frame + 512U && address + DB_BULK_BYTES > frame))
            return I_ESIZE;
        bulk_address = address;
        return I_OK;
    case DB_SCAN:
    case DB_NEXT:
    case DB_PAGE:
        if (building_table >= 0) return I_EBUILD;
        if (bytes != sizeof(struct db_scan)) return I_ESIZE;
        return scan_rows(operation, (struct db_scan *)data);
    }
    if (!table_valid(table))
        return I_ENTBL;
    name = table;
    if (building_table >= 0 && operation != DB_APPEND &&
        operation != DB_FINISH && operation != DB_REBUILD)
        return I_EBUILD;
    switch (operation)
    {
    case DB_INFO:
        if (bytes != sizeof(struct db_info)) return I_ESIZE;
        info = (struct db_info *)data;
        memcpy(info->definition.name, name, I_MXNM);
        memcpy(&info->definition.size, &g_cfg.tbls[table].recsz,
            sizeof(struct db_info) - I_MXNM);
        return I_OK;
    case DB_GET:
        if (!key_valid(table, argument)) return I_EINDEX;
        if (bytes != g_cfg.tbls[table].recsz) return I_ESIZE;
        return idx_find(name, argument, (char *)data, (char *)data);
    case DB_READ:
        return bytes == g_cfg.tbls[table].recsz ?
            i_rdphys(name, (char *)data, argument) : I_ESIZE;
    case DB_DELETE:
        return bytes ? I_ESIZE : i_delphys(name, argument);
    case DB_VERIFY:
        if (bytes != sizeof(struct i_verify)) return I_ESIZE;
        memset(data, 0, bytes);
        return table_verify(name, (struct i_verify *)data);
    case DB_REBUILD:
        if (building_table >= 0 && building_table != table)
            return I_EBUILD;
        return bytes ? I_ESIZE : pki_rebuild(name);
    case DB_BUILD:
        return bytes ? I_ESIZE : begin_build(table);
    case DB_FINISH:
        if (bytes) return I_ESIZE;
        return building_table != table ? I_EBUILD :
            build_indexes(table, g_cfg.tbls[table].maxrec, 1);
    case DB_PICK:
        return bytes == sizeof(struct db_pick) ?
            pick_rows(table, (struct db_pick *)data) : I_ESIZE;
    case DB_APPEND:
    case DB_BATCH:
        if (bytes != 2 * sizeof(int)) return I_ESIZE;
        completed = (int *)data;
        *completed = 0;
        width = g_cfg.tbls[table].recsz;
        completed[1] = width;
        if ((argument && !bulk_address) || argument < 0 || argument > DB_BULK_BYTES ||
            argument % width)
            return I_ESIZE;
        wanted = argument / width;
        if (operation == DB_APPEND)
        {
            rc = append_build(table, (char *)bulk_address, argument);
            if (rc == I_OK)
                *completed = wanted;
            return rc;
        }
        while (*completed < wanted)
        {
            rc = i_insrt(name, (char *)bulk_address + *completed * width, width);
            if (rc != I_OK) return rc;
            ++*completed;
        }
        return I_OK;
    }
    return I_EBANK;
}
