#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "isam.h"
#include "API.H"
#include "BANKLIB.H"
#include "BANKMEM.H"

static int opened;
static bank_memory_handle_t bulk_handle;

static int bank_fail(const char *what, unsigned code)
{
    printf("ISAM %s: %u\r\n", what, code);
    return I_EBANK;
}

static int transport_result(int result)
{
    unsigned error = bank_library_last_error();
    return error == BL_OK ? result : bank_fail("bank transport failed", error);
}

/* A macro so its early returns leave the caller directly. */
#define CALL_GUARD(data, bytes) \
    if ((bytes) > I_RECSZ || ((bytes) && !(data))) \
        return I_ESIZE; \
    if (!opened) \
        return bank_fail("library is not open", 0)

int db_call(int operation, int table, int argument, void *data, unsigned bytes)
{
    CALL_GUARD(data, bytes);
    return transport_result(iscall(operation, table, argument,
        (unsigned char *)data, bytes));
}

int db_send(int operation, int table, int argument, const void *data, unsigned bytes)
{
    CALL_GUARD(data, bytes);
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
        return bank_fail("session is already open or needs buffer cleanup", 0);
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

/* The lease is valid only while its library session and pool epoch are. */
static int lease_live(void)
{
    if (bank_library_check())
        return I_OK;
    /* RESET may give this address to another owner. */
    bulk_handle = 0;
    return bank_fail("bulk buffer lease is stale", bank_library_last_error());
}

static int release_bulk(void)
{
    if (!bulk_handle)
        return I_OK;
    if (lease_live() != I_OK)
        return I_EBANK;
    if (!bank_memory_free(bulk_handle))
        return bank_fail("buffer release failed", bank_memory_last_error());
    bulk_handle = 0;
    return I_OK;
}

int db_close(void)
{
    int rc;
    rc = release_bulk();
    if (opened)
    {
        if (!bank_library_close())
            rc = bank_fail("library unload failed", bank_library_last_error());
        opened = 0;
    }
    return rc;
}

/* Reads need no pre-check: the following library call validates the epoch. */
static int ensure_bulk(int writing)
{
    int rc, bytes;
    if (!opened)
        return bank_fail("library is not open", 0);
    if (bulk_handle)
        return writing ? lease_live() : I_OK;
    bulk_handle = bank_memory_allocate(DB_BULK_BYTES);
    if (!bulk_handle)
        return bank_fail("bulk allocation failed", bank_memory_last_error());
    rc = lease_live();
    if (rc == I_OK)
    {
        bytes = DB_BULK_BYTES;
        rc = db_call(DB_BIND, 0, bulk_handle, &bytes, sizeof(bytes));
    }
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
        return bank_fail("cursor requires an open session", 0);
    return transport_result(iswalk(&scan->cursor, (unsigned char *)record, bytes));
}

int db_first(int table, int key, const char *prefix, int length,
    struct db_scan *scan, char *record, unsigned bytes)
{
    int rc;
    rc = init_scan(table, key, prefix, length, scan);
    return rc == I_OK ? db_walk(scan, record, bytes) : rc;
}

static int bulk_copy(int writing, unsigned offset, void *data, unsigned bytes)
{
    if (writing ? bank_memory_write(bulk_handle, offset, data, bytes) :
        bank_memory_read(bulk_handle, offset, data, bytes))
        return I_OK;
    return bank_fail("bulk transfer failed", bank_memory_last_error());
}

static int fetch_rows(int operation, struct db_scan *scan, char *rows, int capacity)
{
    int rc;
    if (!scan || !rows || capacity < 1)
        return I_ESIZE;
    scan->count = scan->bytes = 0;
    if (capacity > DB_BULK_BYTES)
        capacity = DB_BULK_BYTES;
    rc = ensure_bulk(0);
    if (rc != I_OK)
        return rc;
    scan->bytes = capacity;
    rc = transport_result(iscall(operation, scan->cursor.table_index, 0,
        (unsigned char *)scan, sizeof(*scan)));
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
        return bank_fail("scan returned an invalid completion", 0);
    }
    if (scan->bytes && bulk_copy(0, 0, rows, scan->bytes) != I_OK)
    {
        scan->count = scan->bytes = 0;
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
    rc = ensure_bulk(1);
    if (rc != I_OK)
        return rc;
    rc = bulk_copy(1, DB_BULK_BYTES - input_bytes, (void *)values, input_bytes);
    if (rc != I_OK)
        return rc;
    memset(&request, 0, sizeof(request));
    request.index = index;
    request.count = count;
    request.bytes = capacity;
    request.width = value_size;
    if (view)
        request.view = *view;
    rc = transport_result(iscall(DB_PICK, table, 0, (unsigned char *)&request,
        sizeof(request)));
    if (rc != I_OK)
        return rc;
    if (request.count != count || request.width < 1 || request.width > I_RECSZ ||
        count > (DB_BULK_BYTES - input_bytes) / request.width ||
        count > capacity / request.width || request.bytes != count * request.width)
        return bank_fail("fetch returned an invalid completion", 0);
    return bulk_copy(0, 0, rows, request.bytes);
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
        rc = ensure_bulk(1);
        if (rc == I_OK)
            rc = bulk_copy(1, 0, (void *)records, bytes);
        if (rc != I_OK)
            return rc;
    }
    if (!opened)
        return bank_fail("library is not open", 0);
    memset(reply, 0, sizeof(reply));
    rc = transport_result(iscall(operation, table, bytes, (unsigned char *)reply,
        sizeof(reply)));
    if (rc == I_EBANK || rc == I_ENTBL || rc == I_EBUILD)
        return rc;
    if (reply[1] < 3 || reply[1] > I_RECSZ ||
        reply[0] < 0 || reply[0] > bytes / reply[1] ||
        (rc == I_OK && (bytes % reply[1] || reply[0] != bytes / reply[1])))
        return bank_fail("insert returned an invalid completion", 0);
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
