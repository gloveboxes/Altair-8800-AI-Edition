static void opt_record(char *record, int number)
{
    memset(record, 0, 16);
    record[0] = '0' + number / 1000;
    record[1] = '0' + number / 100 % 10;
    record[2] = '0' + number / 10 % 10;
    record[3] = '0' + number % 10;
    memcpy(record + 5, number % 2 ? "alpha" : "BETA", 5);
}

static int check_optimizations(void)
{
    struct db_scan scan;
    struct db_view view;
    struct i_verify report;
    struct db_info info;
    char record[16], key[12], guard[16];
    int i, count, rc, fd, first;
    CHECK(db_close() == I_OK, 201);
    test_schema("OPTTEST", 16, 4);
    schema.table[0].keys = 2;
    schema.table[0].offsets[1] = 5 | I_KEY_UPPER;
    schema.table[0].widths[1] = 11;
    schema.table[0].extra[1] = 11;
    CHECK(db_new("OPTTEST.CFG", &schema) == I_ESIZE, 202);
    schema.table[0].extra[1] = 3;
    schema.table[0].tails[1] = 14;
    CHECK(db_new("OPTTEST.CFG", &schema) == I_ESIZE, 202);
    schema.table[0].tails[1] = 1;
    CHECK(db_new("OPTTEST.CFG", &schema) == I_OK, 202);
    for (i = 0; i < 257; ++i)
        opt_record(bulk_input + i * 16, 256 - i);
    outp(5, 50);
    for (i = 0; i < 257; ++i)
        CHECK(db_add(0, bulk_input + i * 16, 16) == I_OK, 202);
    outp(5, 178);
    CHECK(db_close() == I_OK && db_new("OPTTEST.CFG", &schema) == I_OK, 202);
    outp(5, 51);
    CHECK(db_build(0) == I_OK && db_build(0) == I_EBUILD, 202);
    CHECK(db_append(0, bulk_input, 4096, &count) == I_OK && count == 256 &&
        db_append(0, bulk_input + 4096, 16, &count) == I_OK && count == 1 &&
        db_read(0, 0, record, 16) == I_EBUILD &&
        db_finish(0) == I_OK, 203);
    outp(5, 179);
    CHECK(db_verify(0, &report) == I_OK &&
        report.active_records == 257, 203);
    CHECK(db_close() == I_OK && db_open("OPTTEST.CFG") == I_OK &&
        db_info(0, &info) == I_OK && info.records == 257 &&
        info.definition.extra[1] == 3 && info.definition.tails[1] == 1 &&
        info.definition.offsets[1] == (5 | I_KEY_UPPER), 204);
    memset(key, 0, sizeof(key));
    memcpy(key, "ALPHA", 5);
    memcpy(key + 8, "001", 3);
    CHECK(db_probe(0, 1, key, 11) == 255, 205);
    CHECK(db_read(0, 255, record, 16) == I_OK, 205);
    opt_record(record, 256);
    record[15] = 'X';
    CHECK(db_put(0, 0, record, 16) == I_OK, 205);
    CHECK(db_read(0, 0, record, 16) == I_OK, 205);
    outp(5, 52);
    rc = db_read(0, 255, record, 16);
    outp(5, 180);
    CHECK(rc == I_OK && !memcmp(record, "0001", 4), 205);
    memset(&view, 0, sizeof(view));
    view.fields = 1; view.offsets[0] = 0; view.sizes[0] = 1;
    memset(guard, 0x5a, sizeof(guard));
    CHECK(db_project(0, 0, 0, 0, &scan, guard + 1, 3, &view) == I_OK &&
        scan.count == 3 && scan.width == 1 && scan.bytes == 3 &&
        guard[0] == 0x5a && guard[4] == 0x5a &&
        db_more(&scan, guard + 1, 3) == I_OK && scan.count == 3, 206);
    view.sizes[0] = 17;
    CHECK(db_project(0, 0, 0, 0, &scan, guard, 16, &view) == I_ESIZE, 207);
    opt_record(record, 1);
    memcpy(record + 5, "ALPHA", 5);
    CHECK(db_put(0, 255, record, 16) == I_OK &&
        db_probe(0, 1, key, 11) == 255 &&
        db_del(0, 254) == I_OK && db_rebuild(0) == I_OK &&
        db_verify(0, &report) == I_OK && report.free_slots == 1, 208);
    CHECK(db_close() == I_OK && db_new("OPTTEST.CFG", &schema) == I_OK &&
        db_build(0) == I_OK &&
        db_append(0, record, 16, &count) == I_OK &&
        db_append(0, record, 16, &count) == I_OK &&
        db_finish(0) == I_EDUP && db_read(0, 0, guard, 16) == I_EBUILD &&
        db_close() == I_OK && db_open("OPTTEST.CFG") == I_EBUILD, 209);
    CHECK(db_new("OPTTEST.CFG", &schema) == I_OK && db_build(0) == I_OK &&
        db_append(0, record, 16, &count) == I_OK && count == 1 &&
        db_close() == I_OK && db_open("OPTTEST.CFG") == I_EBUILD, 209);
    CHECK(db_new("OPTTEST.CFG", &schema) == I_OK &&
        db_build(0) == I_OK && db_finish(0) == I_OK &&
        db_verify(0, &report) == I_OK && !report.active_records &&
        db_close() == I_OK, 210);
    fd = open("OPTTEST.BLD", O_RDONLY);
    CHECK(fd < 0, 211);
    fd = open("OPTTEST.RN0", O_RDONLY);
    CHECK(fd < 0, 212);
    fd = open("OPTTEST.RN1", O_RDONLY);
    CHECK(fd < 0, 213);
    CHECK(unlink("OPTTEST.CFG") == 0 && unlink("OPTTEST.DAT") == 0 &&
        unlink("OPTTEST.IDX") == 0 && unlink("OPTTEST.IX1") == 0, 214);
    test_schema("BIGOPT", 16, 15);
    CHECK(db_new("BIGOPT.CFG", &schema) == I_OK && db_build(0) == I_OK, 215);
    for (first = 0; first < 4000; first += count)
    {
        count = 4000 - first;
        if (count > 256) count = 256;
        for (i = 0; i < count; ++i)
            opt_record(bulk_input + i * 16, 3999 - first - i);
        CHECK(db_append(0, bulk_input, count * 16, &rc) == I_OK && rc == count, 216);
    }
    rc = db_finish(0);
    if (rc != I_OK) printf("Large build finish rc=%d\n", rc);
    CHECK(rc == I_OK, 220);
    CHECK(db_verify(0, &report) == I_OK && report.active_records == 4000, 221);
    CHECK(db_rebuild(0) == I_OK, 222);
    CHECK(db_verify(0, &report) == I_OK, 223);
    opt_record(record, 3999);
    CHECK(db_probe(0, 0, record, 15) == 0 &&
        db_close() == I_OK && db_open("BIGOPT.CFG") == I_OK &&
        db_probe(0, 0, record, 15) == 0 && db_close() == I_OK, 218);
    CHECK(unlink("BIGOPT.CFG") == 0 && unlink("BIGOPT.DAT") == 0 &&
        unlink("BIGOPT.IDX") == 0, 219);
    puts("PASS: composite keys, projected scans, sorted runs, rebuild and failed-build isolation.");
    puts("PASS: sorted build and rebuild cross 64 KiB with 4000 active keys.");
    return I_OK;
}
