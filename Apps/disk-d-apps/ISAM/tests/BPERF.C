#include "FIXTURE.H"
#include "../surgery.h"

static int repeat_lookup(int table, const char *key, int keybytes, int size)
{
    int i, rc;
    for (i = 0; i < 20; ++i)
    {
        if (size)
            rc = find_row(table, 0, key, keybytes, bulk_output, size);
        else
            rc = db_probe(table, 0, key, keybytes);
        if (rc < 0) return I_EVERIFY;
    }
    return I_OK;
}

static int bench_cursor(int key, const char *prefix, int length, int expected, int mode)
{
    struct db_scan scan;
    int rc, total;
    total = 0;
    if (mode < 0)
    {
        rc = db_first(1, key, prefix, length, &scan, bulk_output, PATIENT_SIZE);
        while (rc >= 0)
        {
            total++;
            rc = db_walk(&scan, bulk_output, PATIENT_SIZE);
        }
    }
    else
    {
        rc = db_scan(1, key, prefix, length, &scan, bulk_output, 4096, mode);
        for (;;)
        {
            total += scan.count;
            if (rc != I_OK) break;
            rc = db_more(&scan, bulk_output, 4096);
        }
    }
    return rc == I_ENREC && total == expected ? I_OK : I_EVERIFY;
}

static int performance_run(void)
{
    struct i_stats stats;
    int i, rc;
    CHECK(db_open(CFG_FILE) == I_OK &&
        db_table(DOCTOR_TABLE) == 0 && db_table(PATIENT_TABLE) == 1 &&
        db_table(HISTORY_TABLE) == 2, 111);
    db_reset();
    outp(5, 1);
    for (i = 0; i < 20; ++i) db_reset();
    outp(5, 129);
    CHECK(db_read(0, 0, bulk_output, DOCTOR_SIZE) == I_OK, 112);
    outp(5, 2);
    for (i = 0; i < 20; ++i)
        CHECK(db_read(0, 0, bulk_output, DOCTOR_SIZE) == I_OK, 113);
    outp(5, 130);
    CHECK(repeat_lookup(0, "001", 3, 0) == I_OK, 114);
    outp(5, 3);
    rc = repeat_lookup(0, "001", 3, 0);
    outp(5, 131);
    CHECK(rc == I_OK, 115);
    outp(5, 4);
    rc = repeat_lookup(0, "001", 3, DOCTOR_SIZE);
    outp(5, 132);
    CHECK(rc == I_OK && repeat_lookup(1, "00001", 5, PATIENT_SIZE) == I_OK, 116);
    outp(5, 5);
    rc = repeat_lookup(1, "00001", 5, PATIENT_SIZE);
    outp(5, 133);
    CHECK(rc == I_OK && repeat_lookup(2, "00001001", 8, HISTORY_SIZE) == I_OK, 117);
    outp(5, 6);
    rc = repeat_lookup(2, "00001001", 8, HISTORY_SIZE);
    outp(5, 134);
    CHECK(rc == I_OK, 118);
    outp(5, 7);
    for (i = 0; i < 20; ++i)
        CHECK(find_row(0, 0, "001", 3, bulk_output, DOCTOR_SIZE) >= 0 &&
            find_row(1, 0, "00001", 5, bulk_output, PATIENT_SIZE) >= 0 &&
            find_row(2, 0, "00001001", 8, bulk_output, HISTORY_SIZE) >= 0, 119);
    outp(5, 135);
    CHECK(bench_cursor(0, 0, 0, PATIENT_COUNT, -1) == I_OK, 120);
    outp(5, 8);
    rc = bench_cursor(0, 0, 0, PATIENT_COUNT, -1);
    outp(5, 136);
    CHECK(rc == I_OK, 121);
    outp(5, 9);
    rc = bench_cursor(3, "001", 3, PATIENTS_PER_DOCTOR, -1);
    outp(5, 137);
    CHECK(rc == I_OK && bench_cursor(0, 0, 0, PATIENT_COUNT, DB_RECORDS) == I_OK, 122);
    outp(5, 10);
    rc = bench_cursor(0, 0, 0, PATIENT_COUNT, DB_RECORDS);
    outp(5, 138);
    CHECK(rc == I_OK, 123);
    outp(5, 11);
    rc = bench_cursor(0, 0, 0, PATIENT_COUNT, DB_KEYS);
    outp(5, 139);
    CHECK(rc == I_OK, 124);
    outp(5, 12);
    rc = bench_cursor(3, "001", 3, PATIENTS_PER_DOCTOR, DB_KEYS);
    outp(5, 140);
    CHECK(rc == I_OK && repeat_lookup(1, "00121", 5, PATIENT_SIZE) == I_OK, 125);
    db_reset();
    outp(5, 13);
    rc = repeat_lookup(1, "00121", 5, PATIENT_SIZE);
    outp(5, 141);
    CHECK(rc == I_OK, 126);
    db_stats(&stats);
    printf("MIDPOINT comparisons=%d\r\n", stats.index_comparisons);
    CHECK(db_close() == I_OK, 127);
    puts("PASS: measured ISAM performance workloads.");
    return I_OK;
}
