#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "isam.h"
#include "API.H"
#include "BANKLIB.H"
#include "BANKMEM.H"
#include "BIOSBANK.H"

static int opened;
static bhandle bulk_handle;
static unsigned int bulk_epoch;
static bank_bios_info_t capabilities;
static unsigned char epoch_request[BANK_LIBRARY_REQUEST_BYTES];

static int transport_result(int result)
{
    if (bank_library_last_error() == BL_OK)
        return result;
    printf("ISAM bank transport failed: %u\r\n", bank_library_last_error());
    return I_EBANK;
}

int db_call(int operation, int table, int argument, void *data, unsigned bytes)
{
    if (bytes > I_RECSZ || (bytes && !data))
        return I_ESIZE;
    if (!opened)
    {
        puts("ISAM library is not open.");
        return I_EBANK;
    }
    return transport_result(iscall(operation, table, argument,
        (unsigned char *)data, bytes));
}

int db_send(int operation, int table, int argument, const void *data, unsigned bytes)
{
    if (bytes > I_RECSZ || (bytes && !data))
        return I_ESIZE;
    if (!opened)
    {
        puts("ISAM library is not open.");
        return I_EBANK;
    }
    return transport_result(ispost(operation, table, argument,
        (const unsigned char *)data, bytes));
}

static int name_call(int operation, const char *name)
{
    char buffer[I_MXNM];
    unsigned bytes;
    if (!name || (bytes = strlen(name) + 1) > sizeof(buffer))
        return I_ESIZE;
    memcpy(buffer, name, bytes);
    return db_call(operation, 0, 0, buffer, bytes);
}

int db_open(const char *configuration)
{
    int rc;
    if (opened || bulk_handle)
    {
        puts("ISAM session is already open or needs buffer cleanup.");
        return I_EBANK;
    }
    if (!bank_library_open("ISAM.LIB"))
    {
        printf("Cannot load ISAM.LIB: bank error %u\r\n", bank_library_last_error());
        return I_EBANK;
    }
    opened = 1;
    rc = configuration ? name_call(DB_OPEN, configuration) : I_OK;
    if (rc != I_OK)
    {
        printf("Cannot open ISAM database %s rc=%d\r\n", configuration, rc);
        db_close();
    }
    return rc;
}

int db_new(const char *configuration, const struct db_schema *schema)
{
    int rc;
    if (!configuration || !schema)
        return I_ESIZE;
    rc = db_open(0);
    if (rc != I_OK)
        return rc;
    rc = db_send(DB_DEFINE, 0, 0, schema, sizeof(*schema));
    if (rc == I_OK)
        rc = name_call(DB_CREATE, configuration);
    if (rc != I_OK)
        db_close();
    return rc;
}

int db_table(const char *name)
{
    return name_call(DB_TABLE, name);
}

static int read_epoch(unsigned int *epoch)
{
    memset(epoch_request, 0, sizeof(epoch_request));
    epoch_request[0] = sizeof(epoch_request);
    epoch_request[2] = 1;
    epoch_request[4] = BANK_LIBRARY_QUERY;
    if (!bank_bios_library_control(capabilities.library_request, epoch_request))
    {
        printf("ISAM buffer epoch query failed: %u\r\n", bank_bios_last_error());
        return I_EBANK;
    }
    *epoch = epoch_request[30] | ((unsigned int)epoch_request[31] << 8);
    return I_OK;
}

static int bulk_live(void)
{
    unsigned int epoch;
    if (read_epoch(&epoch) != I_OK)
        return I_EBANK;
    if (epoch != bulk_epoch)
    {
        /* RESET may give this address to another owner. */
        bulk_handle = 0;
        puts("ISAM bulk buffer lease is stale.");
        return I_EBANK;
    }
    if (bsize(bulk_handle) != DB_BULK_BYTES)
    {
        bulk_handle = 0;
        puts("ISAM bulk buffer allocation is invalid.");
        return I_EBANK;
    }
    return I_OK;
}

static int release_bulk(void)
{
    if (!bulk_handle)
        return I_OK;
    if (bulk_live() != I_OK)
        return I_EBANK;
    if (!bfree(bulk_handle))
    {
        printf("ISAM buffer release failed: %u\r\n", bmeror());
        return I_EBANK;
    }
    bulk_handle = 0;
    return I_OK;
}

int db_close(void)
{
    int rc;
    rc = I_OK;
    if (opened)
    {
        if (!bank_library_close())
        {
            printf("Cannot unload ISAM.LIB: bank error %u\r\n",
                bank_library_last_error());
            rc = I_EBANK;
        }
        opened = 0;
    }
    if (release_bulk() != I_OK)
        rc = I_EBANK;
    return rc;
}

static int ensure_bulk(void)
{
    int rc, bytes;
    if (!opened)
    {
        puts("ISAM library is not open.");
        return I_EBANK;
    }
    if (bulk_handle)
        return bulk_live();
    if (!bank_bios_query(&capabilities))
    {
        printf("ISAM buffer capabilities failed: %u\r\n", bank_bios_last_error());
        return I_EBANK;
    }
    rc = read_epoch(&bulk_epoch);
    if (rc != I_OK)
        return rc;
    bulk_handle = bmalloc(DB_BULK_BYTES);
    if (!bulk_handle)
    {
        printf("ISAM bulk allocation failed: %u\r\n", bmeror());
        return I_EBANK;
    }
    bytes = DB_BULK_BYTES;
    rc = db_call(DB_BIND, 0, bulk_handle, &bytes, sizeof(bytes));
    if (rc != I_OK)
        release_bulk();
    return rc;
}

static int init_scan(int table, int key, const char *prefix, int length,
    struct db_scan *scan)
{
    if (!scan || length < 0 || length > I_MXKEYLN || (length && !prefix))
        return I_EINDEX;
    memset(scan, 0, sizeof(*scan));
    scan->cursor.table_index = table;
    scan->cursor.key_index = key;
    scan->cursor.entry_count = -1;
    scan->cursor.prefix_length = length;
    if (length)
        memcpy(scan->cursor.prefix, prefix, length);
    return I_OK;
}

int db_walk(struct db_scan *scan, char *record, unsigned bytes)
{
    if (!opened || !scan)
    {
        puts("ISAM cursor requires an open session and valid state.");
        return I_EBANK;
    }
    return transport_result(iswalk(&scan->cursor, (unsigned char *)record, bytes));
}

int db_first(int table, int key, const char *prefix, int length,
    struct db_scan *scan, char *record, unsigned bytes)
{
    int rc;
    rc = init_scan(table, key, prefix, length, scan);
    return rc == I_OK ? db_walk(scan, record, bytes) : rc;
}

static int fetch_rows(int operation, struct db_scan *scan, char *rows, int capacity)
{
    int rc;
    if (!scan || !rows || capacity < 1)
        return I_ESIZE;
    scan->count = scan->bytes = 0;
    if (capacity > DB_BULK_BYTES)
        capacity = DB_BULK_BYTES;
    rc = ensure_bulk();
    if (rc != I_OK)
        return rc;
    scan->bytes = capacity;
    rc = db_call(operation, scan->cursor.table_index, 0, scan, sizeof(*scan));
    if (rc == I_EBANK)
    {
        scan->count = scan->bytes = 0;
        return rc;
    }
    if (scan->bytes < 0 || scan->bytes > capacity || scan->count < 0 ||
        (scan->count && (scan->width < 1 || scan->width > I_RECSZ ||
         scan->count > capacity / scan->width)) ||
        scan->bytes != scan->count * scan->width)
    {
        scan->count = scan->bytes = 0;
        puts("ISAM scan returned an invalid completion.");
        return I_EBANK;
    }
    if (scan->bytes && !bread(bulk_handle, 0, rows, scan->bytes))
    {
        scan->count = scan->bytes = 0;
        printf("ISAM batch read failed: %u\r\n", bmeror());
        return I_EBANK;
    }
    return rc;
}

int db_scan(int table, int key, const char *prefix, int length,
    struct db_scan *scan, char *rows, int capacity, int mode)
{
    int rc;
    rc = init_scan(table, key, prefix, length, scan);
    if (rc != I_OK)
        return rc;
    scan->mode = mode;
    return fetch_rows(DB_SCAN, scan, rows, capacity);
}

int db_more(struct db_scan *scan, char *rows, int capacity)
{
    return fetch_rows(DB_NEXT, scan, rows, capacity);
}

int db_page(struct db_scan *scan, int position, char *rows, int capacity)
{
    if (scan)
        scan->cursor.position = position;
    return fetch_rows(DB_PAGE, scan, rows, capacity);
}

int db_project(int table, int key, const char *prefix, int length,
    struct db_scan *scan, char *rows, int capacity, const struct db_view *view)
{
    int rc;
    rc = init_scan(table, key, prefix, length, scan);
    if (rc != I_OK)
        return rc;
    if (view)
        scan->view = *view;
    scan->mode = DB_RECORDS;
    return fetch_rows(DB_SCAN, scan, rows, capacity);
}

int db_pick(int table, int index, const void *values, int count, int value_size,
    const struct db_view *view, char *rows, int capacity)
{
    struct db_pick request;
    int rc, input_bytes;
    if (count < 1 || value_size < 1 || value_size > I_MXKEYLN ||
        count > DB_BULK_BYTES / value_size || !values || !rows || capacity < 1)
        return I_ESIZE;
    input_bytes = count * value_size;
    if (capacity > DB_BULK_BYTES)
        capacity = DB_BULK_BYTES;
    rc = ensure_bulk();
    if (rc != I_OK)
        return rc;
    if (!bwrite(bulk_handle, DB_BULK_BYTES - input_bytes, values, input_bytes))
    {
        printf("ISAM selector transfer failed: %u\r\n", bmeror());
        return I_EBANK;
    }
    memset(&request, 0, sizeof(request));
    request.index = index;
    request.count = count;
    request.bytes = capacity;
    request.width = value_size;
    if (view)
        request.view = *view;
    rc = db_call(DB_PICK, table, 0, &request, sizeof(request));
    if (rc != I_OK)
        return rc;
    if (request.count != count || request.width < 1 || request.width > I_RECSZ ||
        count > (DB_BULK_BYTES - input_bytes) / request.width ||
        count > capacity / request.width || request.bytes != count * request.width)
    {
        puts("ISAM fetch returned an invalid completion.");
        return I_EBANK;
    }
    if (!bread(bulk_handle, 0, rows, request.bytes))
    {
        printf("ISAM fetch read failed: %u\r\n", bmeror());
        return I_EBANK;
    }
    return I_OK;
}

static int send_batch(int operation, int table, const char *records,
    int bytes, int *completed)
{
    int rc, reply[2];
    if (!completed)
        return I_ESIZE;
    *completed = 0;
    if (bytes < 0 || bytes > DB_BULK_BYTES || (bytes && !records))
        return I_ESIZE;
    if (bytes)
    {
        rc = ensure_bulk();
        if (rc != I_OK)
            return rc;
        if (!bwrite(bulk_handle, 0, records, bytes))
        {
            printf("ISAM batch write failed: %u\r\n", bmeror());
            return I_EBANK;
        }
    }
    memset(reply, 0, sizeof(reply));
    rc = db_call(operation, table, bytes, reply, sizeof(reply));
    if (rc == I_EBANK || rc == I_ENTBL || rc == I_EBUILD)
        return rc;
    if (reply[1] < 3 || reply[1] > I_RECSZ ||
        reply[0] < 0 || reply[0] > bytes / reply[1] ||
        (rc == I_OK && (bytes % reply[1] || reply[0] != bytes / reply[1])))
    {
        puts("ISAM insert returned an invalid completion.");
        return I_EBANK;
    }
    *completed = reply[0];
    return rc;
}

int db_batch(int table, const char *records, int bytes, int *completed)
{
    return send_batch(DB_BATCH, table, records, bytes, completed);
}

int db_append(int table, const char *records, int bytes, int *completed)
{
    return send_batch(DB_APPEND, table, records, bytes, completed);
}

static void require_result(int rc)
{
    if (rc != I_OK)
    {
        printf("ISAM service failed rc=%d\r\n", rc);
        db_close();
        exit(1);
    }
}

void db_reset(void) { require_result(db_call(DB_RESET, 0, 0, 0, 0)); }
void db_stats(struct i_stats *stats)
{ require_result(db_call(DB_STATS, 0, 0, stats, sizeof(*stats))); }
