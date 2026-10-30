#include "stdio.h"
#include "string.h"
#include "fcntl.h"
#include "unistd.h"
#include "../isam.h"

#define CHECK(test, code) if (!(test)) return code
#define ISAM_PAGE_TEST
#include "BULKTST.C"

static int index_work(void)
{
    struct db_scan scan, other;
    struct i_stats stats;
    char keys[80];
    int i, order, rc;
    CHECK(db_scan(0, 1, 0, 0, &scan, bulk_output, 680, DB_KEYS) == I_OK, 211);
    db_reset();
    outp(5, 60);
    rc = db_scan(0, 1, 0, 0, &scan, bulk_output, 680, DB_KEYS);
    outp(5, 188);
    CHECK(rc == I_OK && scan.count == 40, 211);
    for (i = 0; i < 40; ++i)
        CHECK(!memcmp(bulk_output + i * 17, bulk_input + i * I_RECSZ + 4, 15) &&
            bulk_output[i * 17 + 15] == i, 212);
    CHECK(db_scan(0, 1, 0, 0, &scan, bulk_output, 17, DB_KEYS) == I_OK &&
        db_scan(0, 1, 0, 0, &other, bulk_output, 17, DB_KEYS) == I_OK &&
        db_page(&other, 35, bulk_output, 17) == I_OK &&
        db_more(&scan, bulk_output, 34) == I_OK &&
        bulk_output[15] == 1 && bulk_output[32] == 2, 213);
    for (order = 0; order < 2; ++order)
    {
        for (i = 0; i < 20; ++i)
            memcpy(keys + i * 4, bulk_input + (order ? 19 - i : i) * I_RECSZ, 4);
        CHECK(db_lookup(0, 0, keys, 20, 4, 0, bulk_output, 2560) == I_OK, 214);
        db_reset();
        outp(5, order ? 59 : 58);
        rc = db_lookup(0, 0, keys, 20, 4, 0, bulk_output, 2560);
        outp(5, order ? 187 : 186);
        CHECK(rc == I_OK, 214);
        db_stats(&stats);
        printf("LOOKUP order=%d comparisons=%d\r\n", order, stats.index_comparisons);
        CHECK(stats.exact_lookups == 20 && stats.physical_reads == 20 &&
            !stats.table_scans &&
            stats.index_comparisons <= (order ? 94 : 25), 215);
        for (i = 0; i < 20; ++i)
            CHECK(!memcmp(bulk_output + i * I_RECSZ,
                bulk_input + (order ? 19 - i : i) * I_RECSZ, I_RECSZ), 215);
    }
    memcpy(keys, "0000000000010019003900390008", 28);
    CHECK(db_lookup(0, 0, keys, 7, 4, 0, bulk_output, 896) == I_OK, 216);
    for (i = 0; i < 7; ++i)
        CHECK(!memcmp(bulk_output + i * I_RECSZ, keys + i * 4, 4), 216);
    CHECK(db_del(1, 10) == I_OK, 217);
    memcpy(keys, "000900100011", 12);
    memset(bulk_output, 0x5a, sizeof(bulk_output));
    CHECK(db_lookup(1, 0, keys, 3, 4, 0, bulk_output, 384) == I_ENREC, 217);
    for (i = 0; i < sizeof(bulk_output); ++i)
        CHECK(bulk_output[i] == 0x5a, 218);
    memcpy(keys, "0009001X", 8);
    CHECK(db_lookup(0, 0, keys, 2, 4, 0, bulk_output, 256) == I_ENREC, 218);
    puts("PASS: streamed indexes, interleaved cursors, ordered and unordered lookups.");
    return I_OK;
}

static int stream_bounds(void)
{
    static struct db_schema schema;
    static char header[24], entries[119];
    static struct db_scan scan;
    static struct i_stats stats;
    char *entry;
    unsigned sum;
    int fd, first, i, digit, value, rc;
    CHECK(db_close() == I_OK, 220);
    memset(&schema, 0, sizeof(schema));
    strcpy(schema.name, "GAPS");
    schema.tables = 1;
    strcpy(schema.table[0].name, "GAPS");
    schema.table[0].size = 16;
    schema.table[0].keys = 1;
    schema.table[0].widths[0] = 15;
    CHECK(db_new("GAPS.CFG", &schema) == I_OK &&
        db_add(0, "0000ABCDEFGHIJK", 16) == I_OK &&
        db_add(0, "3999ABCDEFGHIJK", 16) == I_OK &&
        db_close() == I_OK, 220);
    fd = open("GAPS.IDX", O_RDWR);
    CHECK(fd >= 0 && read(fd, header, 24) == 24, 221);
    header[10] = 0xa0; header[11] = 0x0f;
    sum = 0;
    for (i = 0; i < 20; ++i)
        sum += (unsigned char)header[i];
    header[20] = sum; header[21] = sum >> 8;
    CHECK(lseek(fd, 0L, 0) == 0L && write(fd, header, 24) == 24, 221);
    for (first = 0; first < 4000; first += i)
    {
        for (i = 0; i < 7 && first + i < 4000; ++i)
        {
            entry = entries + i * 17;
            memcpy(entry, "0000ABCDEFGHIJK", 15);
            value = first + i;
            for (digit = 3; digit >= 0; --digit)
            {
                entry[digit] = '0' + value % 10;
                value /= 10;
            }
            entry[15] = entry[16] = 0xff;
            if (first + i == 0 || first + i == 3999)
            {
                entry[15] = first + i == 0 ? 0 : 1;
                entry[16] = 0;
            }
        }
        CHECK(write(fd, entries, i * 17) == i * 17, 221);
    }
    CHECK(close(fd) == 0 && db_open("GAPS.CFG") == I_OK, 222);
    CHECK(db_scan(0, 0, 0, 0, &scan, bulk_output, 51, DB_KEYS) == I_ENREC &&
        scan.count == 2 && scan.cursor.position == 4000 &&
        !memcmp(bulk_output, "0000ABCDEFGHIJK", 15) &&
        !memcmp(bulk_output + 17, "3999ABCDEFGHIJK", 15) &&
        db_page(&scan, 3853, bulk_output, 17) == I_OK &&
        !memcmp(bulk_output, "3999ABCDEFGHIJK", 15), 222);
    memcpy(entries, "0000ABCDEFGHIJK3999ABCDEFGHIJK", 30);
    db_reset();
    CHECK(db_lookup(0, 0, entries, 2, 15, 0, bulk_output, 32) == I_OK &&
        !memcmp(bulk_output + 16, "3999ABCDEFGHIJK", 15), 223);
    db_stats(&stats);
    CHECK(stats.index_comparisons <= 30 && stats.physical_reads == 2, 223);
    CHECK(db_close() == I_OK, 224);
    /* Fill the CP/M sector with one live entry and tombstones before EOF. */
    memset(entries, 0xff, 102);
    memcpy(entries, "0000ABCDEFGHIJK", 15);
    entries[15] = entries[16] = 0;
    fd = open("GAPS.IDX", O_WRONLY | O_TRUNC);
    CHECK(fd >= 0 && write(fd, header, 24) == 24 &&
        write(fd, entries, 102) == 102 && close(fd) == 0 &&
        db_open("GAPS.CFG") == I_OK, 224);
    rc = db_scan(0, 0, 0, 0, &scan, bulk_output, 4096, DB_KEYS);
    CHECK(rc == I_EREAD, 225);
    puts("PASS: index streaming across 64 KiB, sparse lookup fallback and short reads.");
    return I_OK;
}

static int run_pages(void)
{
    static struct db_schema schema;
    struct db_scan scan;
    int i, table, count;
    memset(&schema, 0, sizeof(schema));
    strcpy(schema.name, "PAGES");
    schema.tables = 2;
    strcpy(schema.table[0].name, "PAGEA");
    schema.table[0].size = I_RECSZ;
    schema.table[0].keys = 2;
    schema.table[0].widths[0] = 4;
    schema.table[0].offsets[1] = 4;
    schema.table[0].widths[1] = 15;
    schema.table[1] = schema.table[0];
    strcpy(schema.table[1].name, "PAGEB");
    CHECK(db_new("PAGES.CFG", &schema) == I_OK, 200);
    CHECK(db_scan(0, 0, 0, 0, &scan, bulk_output, 128, DB_RECORDS) == I_ENREC &&
        db_page(&scan, 0, bulk_output, 128) == I_ENREC && !scan.count, 200);
    for (i = 0; i < 40; ++i)
        bulk_record(bulk_input + i * I_RECSZ, i);
    for (table = 0; table < 2; ++table)
        CHECK(db_batch(table, bulk_input, 4096, &count) == I_OK && count == 32 &&
            db_batch(table, bulk_input + 4096, 1024, &count) == I_OK &&
            count == 8, 200);
    return check_pages();
}

int main(void)
{
    int rc;
    rc = run_pages();
    if (!rc)
        rc = index_work();
    if (!rc)
        rc = stream_bounds();
    if (db_close() != I_OK && !rc)
        rc = 210;
    if (rc)
        printf("FAIL: indexed paging rc=%d\r\n", rc);
    return rc;
}
