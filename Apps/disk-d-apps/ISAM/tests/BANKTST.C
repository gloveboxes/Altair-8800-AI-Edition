#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "fcntl.h"
#include "unistd.h"
#include "../isam.h"
#include "API.H"
#include "BANKLIB.H"
#include "BANKMEM.H"

#define CHECK(test, code) if (!(test)) return code
static struct db_schema schema;

static void test_schema(const char *name, int size, int width)
{
    memset(&schema, 0, sizeof(schema));
    strcpy(schema.name, name);
    schema.tables = 1;
    strcpy(schema.table[0].name, name);
    schema.table[0].size = size;
    schema.table[0].keys = 1;
    schema.table[0].widths[0] = width;
}

static int find_row(int table, int key, const char *value, int width,
    char *record, int size)
{
    memcpy(record, value, width);
    return db_get(table, key, record, size);
}

static void make_rec(char *record, const char *key, const char *value)
{
    memset(record, 0, 16);
    memcpy(record, key, 4);
    strncpy(record + 5, value, 8);
}

#include "NAUDIT.C"
#include "BULKTST.C"
#include "BPERF.C"
#include "OPTST.C"

static int check_transfer(void)
{
    unsigned char invalid[129];
    struct db_scan scan;
    struct db_info info;
    struct i_verify report;
    struct i_stats stats;
    char input[I_RECSZ];
    struct { char before, record[I_RECSZ], after; } guarded;
    int i, rc;

    CHECK(db_close() == I_OK, 60);
    test_schema("BOUNDARY", I_RECSZ, 4);
    schema.tables = I_MXTBL + 1;
    CHECK(db_new("BOUNDARY.CFG", &schema) == I_ESIZE, 60);
    schema.tables = 2;
    schema.table[1] = schema.table[0];
    CHECK(db_new("BOUNDARY.CFG", &schema) == I_ESIZE, 60);
    schema.tables = 1;
    CHECK(db_new("BOUNDARY.CFG", &schema) == I_OK, 61);
    CHECK(db_table("BOUNDARY") == 0 && db_table("ABSENT") == I_ENTBL, 62);
    memset(invalid, 0x5a, sizeof(invalid));
    CHECK(iscall(DB_STATS, 0, 0, invalid, 129) == 0 &&
        bank_library_last_error() == BL_BOUNDS &&
        iscall(DB_STATS, 0, 0, 0, 1) == 0 &&
        bank_library_last_error() == BL_ARGUMENT, 63);
    CHECK(db_call(DB_STATS, 0, 0, invalid, 1) == I_ESIZE &&
        db_call(DB_RESET, 0, 0, invalid, 1) == I_ESIZE &&
        db_call(120, 0, 0, 0, 0) == I_EBANK &&
        db_call(-1, 0, 0, invalid, 1) == I_EBANK &&
        bank_library_last_error() == BL_OK, 64);
    for (i = 0; i < sizeof(invalid); ++i)
        CHECK(invalid[i] == 0x5a, 65);
    for (i = 0; i < I_RECSZ; ++i)
        input[i] = i + 1;
    CHECK(db_add(0, input, I_RECSZ) == I_OK &&
        db_add(0, input, I_RECSZ - 1) == I_ESIZE &&
        db_add(0, input, I_RECSZ + 1) == I_ESIZE &&
        db_add(0, input, I_RECSZ) == I_EDUP, 66);
    memset(&guarded, 0x5a, sizeof(guarded));
    CHECK(find_row(0, 0, input, 4, guarded.record, I_RECSZ) == 0 &&
        !memcmp(input, guarded.record, I_RECSZ) &&
        guarded.before == 0x5a && guarded.after == 0x5a &&
        db_probe(0, 0, input, 4) == 0, 67);
    CHECK(db_first(0, 0, 0, 0, &scan, guarded.record, I_RECSZ) == 0 &&
        db_walk(&scan, guarded.record, I_RECSZ) == I_ENREC &&
        db_first(0, 0, input, 16, &scan, guarded.record, I_RECSZ) == I_EINDEX, 68);
    input[127] = 'Z';
    CHECK(db_put(0, 0, input, I_RECSZ) == I_OK &&
        db_read(0, 0, guarded.record, I_RECSZ) == I_OK &&
        guarded.record[127] == 'Z' &&
        db_del(0, 0) == I_OK &&
        db_read(0, 0, guarded.record, I_RECSZ) == I_ENREC &&
        db_add(0, input, I_RECSZ) == I_OK &&
        db_info(0, &info) == I_OK && info.records == 1 && info.slots == 1 &&
        db_verify(0, &report) == I_OK && !report.errors, 69);
    db_stats(&stats);
    CHECK(stats.exact_lookups && stats.physical_reads, 70);
    db_reset();
    db_stats(&stats);
    CHECK(!stats.exact_lookups && !stats.physical_reads, 71);
    CHECK(db_close() == I_OK && db_open("BOUNDARY.CFG") == I_OK &&
        find_row(0, 0, input, 4, guarded.record, I_RECSZ) == 0 &&
        !memcmp(input, guarded.record, I_RECSZ), 72);
    rc = open("BOUNDARY.JRN", O_RDONLY);
    CHECK(rc < 0, 73);
    CHECK(db_close() == I_OK && unlink("BOUNDARY.DAT") == 0 &&
        db_open("BOUNDARY.CFG") == I_EOPEN, 73);
    rc = db_close();
    CHECK(rc == I_OK, 74);
    puts("PASS: copied buffers, 128-byte records, signed errors, counts, stats and reload.");
    return I_OK;
}

static int check_indexes(void)
{
    struct db_scan scan;
    struct i_verify report;
    char record[16], output[16], corrupt;
    int fd;
    test_schema("PKEY", 16, 4);
    schema.table[0].keys = 2;
    schema.table[0].offsets[1] = 5;
    schema.table[0].widths[1] = 8;
    CHECK(db_new("PKTEST.CFG", &schema) == I_OK, 1);
    make_rec(record, "0030", "THIRTY");
    CHECK(db_add(0, record, 16) == I_OK, 2);
    make_rec(record, "0010", "TEN");
    CHECK(db_add(0, record, 16) == I_OK, 3);
    make_rec(record, "0020", "TWENTY");
    CHECK(db_add(0, record, 16) == I_OK &&
        find_row(0, 1, record + 5, 8, output, 16) == 2, 4);
    CHECK(db_first(0, 0, 0, 0, &scan, output, 16) == 1 &&
        db_walk(&scan, output, 16) == 2 && db_walk(&scan, output, 16) == 0 &&
        db_walk(&scan, output, 16) == I_ENREC, 5);
    make_rec(record, "0025", "UPDATED");
    CHECK(db_put(0, 2, record, 16) == I_OK &&
        db_probe(0, 0, "0020", 4) == I_ENREC &&
        db_del(0, 1) == I_OK, 6);
    make_rec(record, "0040", "REUSED");
    CHECK(db_add(0, record, 16) == I_OK &&
        db_probe(0, 0, "0040", 4) == 1 &&
        db_verify(0, &report) == I_OK && report.active_records == 3, 7);
    fd = open("PKEY.IDX", O_RDWR);
    corrupt = '9';
    CHECK(fd >= 0 && lseek(fd, 24L, 0) >= 0L &&
        write(fd, &corrupt, 1) == 1 && close(fd) == 0 &&
        db_verify(0, &report) == I_EVERIFY &&
        db_rebuild(0) == I_OK && db_verify(0, &report) == I_OK, 8);
    CHECK(db_close() == I_OK, 16);
    puts("SUCCESS: multi-key insert, update, delete, ordered cursor and rebuild");
    return I_OK;
}

int main(int argc, char **argv)
{
    int rc;
    if (argc > 1 && !strcmp(argv[1], "/P"))
        rc = performance_run();
    else
    {
        CHECK(db_open("ABSENT.CFG") == I_EOPEN, 76);
        rc = native_audit();
        if (!rc) rc = check_indexes();
        if (!rc) rc = check_transfer();
        if (!rc) rc = check_bulk();
        if (!rc) rc = check_optimizations();
    }
    if (db_close() != I_OK && !rc)
        rc = 75;
    if (rc)
        printf("FAIL: banked ISAM regression rc=%d\r\n", rc);
    else
        puts("PASS: banked ISAM regression.");
    return rc;
}
