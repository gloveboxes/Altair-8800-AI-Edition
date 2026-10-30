static char bulk_input[40 * I_RECSZ];
static char bulk_output[4098];

static void bulk_record(char *record, int number)
{
    memset(record, 'A', I_RECSZ);
    record[0] = record[1] = '0';
    record[2] = '0' + number / 10;
    record[3] = '0' + number % 10;
    memcpy(record + 15, record, 4);
}

#ifndef ISAM_PAGE_TEST
static int check_fetch(void)
{
    int slots[32];
    int i;
    struct db_pick request;
    struct i_stats stats;
    for (i = 0; i < 32; ++i)
        slots[i] = 39 - i;
    memset(bulk_output, 0x5a, sizeof(bulk_output));
    db_reset();
    CHECK(db_fetch(0, slots, 31, 0, bulk_output + 1, 31 * I_RECSZ) == I_OK, 171);
    for (i = 0; i < 31; ++i)
        CHECK(!memcmp(bulk_output + 1 + i * I_RECSZ,
            bulk_input + slots[i] * I_RECSZ, I_RECSZ), 172);
    CHECK(bulk_output[0] == 0x5a &&
        bulk_output[31 * I_RECSZ + 1] == 0x5a, 173);
    db_stats(&stats);
    CHECK(stats.physical_reads == 31 && !stats.exact_lookups, 174);
    slots[1] = slots[0];
    CHECK(db_fetch(0, slots, 2, 0, bulk_output, 256) == I_OK &&
        !memcmp(bulk_output, bulk_output + I_RECSZ, I_RECSZ), 175);
    CHECK(db_fetch(0, slots, 32, 0, bulk_output, 4096) == I_ESIZE &&
        db_fetch(0, slots, 2, 0, bulk_output, 255) == I_ESIZE &&
        db_fetch(0, slots, 0, 0, bulk_output, 4096) == I_ESIZE &&
        db_fetch(0, 0, 1, 0, bulk_output, 4096) == I_ESIZE &&
        db_fetch(-1, slots, 1, 0, bulk_output, 4096) == I_ENTBL, 176);
    memset(bulk_output, 0x5a, sizeof(bulk_output));
    slots[1] = -1;
    CHECK(db_fetch(0, slots, 2, 0, bulk_output, 256) == I_ENREC, 177);
    for (i = 0; i < sizeof(bulk_output); ++i)
        CHECK(bulk_output[i] == 0x5a, 178);
    memset(&request, 0, sizeof(request));
    request.index = DB_PHYSICAL;
    request.count = 32767;
    request.width = 2;
    request.bytes = 4096;
    CHECK(db_call(DB_PICK, 0, 0, &request, sizeof(request)) == I_ESIZE &&
        db_call(DB_PICK, 0, 0, &request, 1) == I_ESIZE, 179);
    puts("PASS: packed fetch order, duplicate slots, guards and invalid requests.");
    return I_OK;
}

static int check_views(void)
{
    struct db_view view;
    char keys[60];
    int slots[4], i, field;
    slots[0] = 39; slots[1] = 0; slots[2] = 39; slots[3] = 20;
    for (i = 0; i < 4; ++i)
        memcpy(keys + i * 4, bulk_input + slots[i] * I_RECSZ, 4);
    CHECK(db_lookup(0, 0, keys, 4, 4, 0, bulk_output, 512) == I_OK, 181);
    for (i = 0; i < 4; ++i)
        CHECK(!memcmp(bulk_output + i * I_RECSZ,
            bulk_input + slots[i] * I_RECSZ, I_RECSZ), 182);
    memset(&view, 0, sizeof(view));
    view.fields = 3;
    view.offsets[0] = 127; view.sizes[0] = 1;
    view.offsets[1] = 0; view.sizes[1] = 4;
    view.offsets[2] = 4; view.sizes[2] = 15;
    memset(bulk_output, 0x5a, sizeof(bulk_output));
    CHECK(db_fetch(0, slots, 4, &view, bulk_output + 1, 80) == I_OK, 183);
    for (i = 0; i < 4; ++i)
        CHECK(bulk_output[1 + i * 20] == 'A' &&
            !memcmp(bulk_output + 2 + i * 20,
                bulk_input + slots[i] * I_RECSZ, 19), 184);
    CHECK(bulk_output[0] == 0x5a && bulk_output[81] == 0x5a, 185);
    for (i = 0; i < 4; ++i)
        memcpy(keys + i * 15, bulk_input + slots[i] * I_RECSZ + 4, 15);
    CHECK(db_lookup(0, 1, keys, 4, 15, &view, bulk_output, 80) == I_OK, 186);
    for (i = 0; i < 4; ++i)
        CHECK(!memcmp(bulk_output + 1 + i * 20,
            bulk_input + slots[i] * I_RECSZ, 19), 187);
    memcpy(bulk_output, keys, 60);
    CHECK(db_lookup(0, 1, bulk_output, 4, 15, &view, bulk_output, 80) == I_OK &&
        !memcmp(bulk_output + 1, bulk_input + 39 * I_RECSZ, 19), 188);
    memcpy(keys + 30, "MISSINGSECONDAR", 15);
    memset(bulk_output, 0x5a, sizeof(bulk_output));
    CHECK(db_lookup(0, 1, keys, 4, 15, &view, bulk_output, 80) == I_ENREC, 189);
    for (i = 0; i < sizeof(bulk_output); ++i)
        CHECK(bulk_output[i] == 0x5a, 190);
    CHECK(db_lookup(0, 0, keys, 4, 5, &view, bulk_output, 80) == I_ESIZE &&
        db_lookup(0, 2, keys, 4, 4, &view, bulk_output, 80) == I_EINDEX &&
        db_fetch(0, slots, 4, &view, bulk_output, 79) == I_ESIZE, 191);
    for (field = -1; field <= DB_FIELDS + 1; field += DB_FIELDS + 2)
    {
        view.fields = field;
        CHECK(db_fetch(0, slots, 1, &view, bulk_output, 128) == I_ESIZE, 192);
    }
    view.fields = 1;
    view.offsets[0] = -1;
    CHECK(db_fetch(0, slots, 1, &view, bulk_output, 128) == I_ESIZE, 193);
    view.offsets[0] = 127; view.sizes[0] = 2;
    CHECK(db_fetch(0, slots, 1, &view, bulk_output, 128) == I_ESIZE, 194);
    view.offsets[0] = 0; view.sizes[0] = 255;
    CHECK(db_fetch(0, slots, 1, &view, bulk_output, 128) == I_ESIZE, 195);
    view.fields = DB_FIELDS;
    for (i = 0; i < DB_FIELDS; ++i)
    {
        view.offsets[i] = 0; view.sizes[i] = 1;
    }
    CHECK(db_fetch(0, slots, 4, &view, bulk_output, 24) == I_OK, 196);
    for (i = 0; i < 24; ++i)
        CHECK(bulk_output[i] == '0', 197);
    view.fields = 2;
    view.sizes[0] = view.sizes[1] = 128;
    CHECK(db_fetch(0, slots, 1, &view, bulk_output, 4096) == I_ESIZE, 198);
    puts("PASS: batched primary/secondary keys, projected ranges, aliasing and bounds.");
    return I_OK;
}

#else
static int check_pages(void)
{
    struct db_scan scan;
    struct db_view view;
    struct i_stats stats;
    int i, position;
    CHECK(db_scan(0, 0, 0, 0, &scan, bulk_output,
        5 * I_RECSZ, DB_RECORDS) == I_OK && scan.count == 5, 201);
    outp(5, 57);
    CHECK(db_page(&scan, 35, bulk_output, 5 * I_RECSZ) == I_OK, 201);
    outp(5, 185);
    for (position = 0; position <= 35; position += 35)
    {
        CHECK(db_page(&scan, position, bulk_output, 5 * I_RECSZ) == I_OK, 202);
        memset(bulk_output, 0x5a, sizeof(bulk_output));
        db_reset();
        outp(5, position ? 56 : 55);
        CHECK(db_page(&scan, position, bulk_output + 1, 5 * I_RECSZ) == I_OK &&
            scan.count == 5 && scan.bytes == 5 * I_RECSZ &&
            scan.cursor.position == position + 4 &&
            !memcmp(bulk_output + 1, bulk_input + position * I_RECSZ,
                5 * I_RECSZ) && bulk_output[0] == 0x5a &&
            bulk_output[5 * I_RECSZ + 1] == 0x5a, 202);
        outp(5, position ? 184 : 183);
        db_stats(&stats);
        CHECK(stats.cursor_rows == 5 && stats.physical_reads == 5 &&
            !stats.cursor_starts && !stats.index_comparisons &&
            !stats.table_scans && !stats.scan_slots, 203);
    }
    CHECK(db_more(&scan, bulk_output, 128) == I_ENREC && !scan.count &&
        db_page(&scan, 38, bulk_output, 640) == I_ENREC && scan.count == 2 &&
        !memcmp(bulk_output, bulk_input + 38 * I_RECSZ, 256) &&
        db_page(&scan, 0, bulk_output, 128) == I_OK && scan.count == 1 &&
        db_more(&scan, bulk_output, 128) == I_OK &&
        !memcmp(bulk_output, bulk_input + I_RECSZ, I_RECSZ), 204);
    CHECK(db_page(&scan, 40, bulk_output, 128) == I_ENREC && !scan.count &&
        db_page(&scan, 41, bulk_output, 128) == I_EINDEX &&
        db_page(&scan, -1, bulk_output, 128) == I_EINDEX &&
        db_page(0, 0, bulk_output, 128) == I_ESIZE &&
        db_page(&scan, 0, bulk_output, 127) == I_ESIZE &&
        db_page(&scan, 0, 0, 128) == I_ESIZE, 205);
    scan.cursor.position = -1;
    scan.bytes = 128;
    CHECK(db_call(DB_PAGE, 0, 0, &scan, sizeof(scan)) == I_EINDEX &&
        db_call(DB_PAGE, 0, 0, &scan, 1) == I_ESIZE, 206);
    scan.cursor.entry_count = -1;
    CHECK(db_page(&scan, 0, bulk_output, 128) == I_EINDEX, 206);
    memset(&view, 0, sizeof(view));
    view.fields = 1; view.offsets[0] = 0; view.sizes[0] = 4;
    CHECK(db_project(0, 1, 0, 0, &scan, bulk_output, 20, &view) == I_OK &&
        db_page(&scan, 35, bulk_output, 20) == I_OK &&
        scan.count == 5 && scan.width == 4, 207);
    for (i = 0; i < 5; ++i)
        CHECK(!memcmp(bulk_output + i * 4, bulk_input + (35 + i) * I_RECSZ, 4), 207);
    CHECK(db_scan(0, 0, "002", 3, &scan, bulk_output, 6, DB_KEYS) == I_OK &&
        scan.cursor.position == 20 &&
        db_page(&scan, 28, bulk_output, 30) == I_ENREC && scan.count == 2 &&
        !memcmp(bulk_output, "0028", 4) &&
        !memcmp(bulk_output + 6, "0029", 4) &&
        db_page(&scan, 20, bulk_output, 6) == I_OK &&
        !memcmp(bulk_output, "0020", 4), 208);
    CHECK(db_del(1, 10) == I_OK &&
        db_scan(1, 0, 0, 0, &scan, bulk_output, 2, DB_SLOTS) == I_OK &&
        db_page(&scan, 10, bulk_output, 4) == I_OK && scan.count == 2 &&
        bulk_output[0] == 11 && bulk_output[2] == 12 &&
        scan.cursor.position == 12 &&
        db_more(&scan, bulk_output, 2) == I_OK && bulk_output[0] == 13 &&
        db_add(1, bulk_input + 10 * I_RECSZ, I_RECSZ) == I_OK, 209);
    puts("PASS: indexed pages, constant-depth cost, bookmarks, prefixes and tombstones.");
    return I_OK;
}
#endif

#ifndef ISAM_PAGE_TEST
static int check_bulk(void)
{
    struct db_scan scan;
    struct db_info info;
    struct i_stats stats;
    struct i_verify report;
    int rc, count, i, available, total, phase, bytes;
    char last_key[4];
    bhandle witness;

    test_schema("BULK", I_RECSZ, 4);
    schema.table[0].keys = 2;
    schema.table[0].offsets[1] = 4;
    schema.table[0].widths[1] = 15;
    schema.tables = 2;
    schema.table[1] = schema.table[0];
    strcpy(schema.table[1].name, "SINGLE");
    CHECK(db_new("BULK.CFG", &schema) == I_OK, 81);
    available = bavail();
    CHECK(db_batch(0, 0, 0, &count) == I_OK && !count &&
        bavail() == available &&
        db_batch(-1, bulk_input, 128, &count) == I_ENTBL &&
        db_batch(0, 0, 128, &count) == I_ESIZE &&
        db_batch(0, bulk_input, 4097, &count) == I_ESIZE, 82);
    memset(bulk_output, 0x5a, sizeof(bulk_output));
    CHECK(db_scan(0, 0, 0, 0, &scan, bulk_output + 1,
        4096, DB_RECORDS) == I_ENREC && !scan.count, 83);
    for (i = 0; i < 40; ++i)
        bulk_record(bulk_input + i * I_RECSZ, i);
    outp(5, 20);
    for (i = 0; i < 40; ++i)
        CHECK(db_add(1, bulk_input + i * I_RECSZ, I_RECSZ) == I_OK, 104);
    outp(5, 148);
    outp(5, 21);
    CHECK(db_batch(0, bulk_input, 4096, &count) == I_OK && count == 32 &&
        db_batch(0, bulk_input + 4096, 8 * I_RECSZ, &count) == I_OK &&
        count == 8, 84);
    outp(5, 149);
    CHECK(db_info(0, &info) == I_OK && info.records == 40 &&
        info.slots == 40 && bavail() == available - DB_BULK_BYTES, 85);
    rc = check_fetch();
    if (rc != I_OK)
        return rc;
    rc = check_views();
    if (rc != I_OK)
        return rc;
    memset(bulk_output, 0x5a, sizeof(bulk_output));
    CHECK(db_scan(0, 0, 0, 0, &scan, bulk_output + 1,
        4096, DB_RECORDS) == I_OK && scan.count == 32 &&
        !memcmp(bulk_output + 1, bulk_input, 4096) &&
        bulk_output[0] == 0x5a && bulk_output[4097] == 0x5a, 86);
    memset(bulk_output, 0x5a, sizeof(bulk_output));
    CHECK(db_more(&scan, bulk_output + 1, 4096) == I_ENREC && scan.count == 8 &&
        !memcmp(bulk_output + 1, bulk_input + 4096, 8 * I_RECSZ) &&
        bulk_output[8 * I_RECSZ + 1] == 0x5a &&
        db_more(&scan, bulk_output + 1, 4096) == I_ENREC && !scan.count, 87);
    db_reset();
    for (i = 0; i < 20; ++i)
        CHECK(db_probe(0, 0, bulk_input, 4) == 0, 88);
    db_stats(&stats);
    CHECK(!stats.physical_reads && stats.exact_lookups == 20, 89);
    db_reset();
    CHECK(db_scan(0, 1, "A", 1, &scan, bulk_output + 1, 4096, DB_KEYS) == I_ENREC &&
        scan.count == 40 && scan.width == 17, 90);
    for (i = 0; i < scan.count; ++i)
        CHECK(!memcmp(bulk_output + 1 + i * 17, bulk_input + i * I_RECSZ + 4, 15) &&
            (unsigned char)bulk_output[1 + i * 17 + 15] == i &&
            !bulk_output[1 + i * 17 + 16], 91);
    CHECK(db_scan(0, 0, 0, 0, &scan, bulk_output + 1, 4096, DB_SLOTS) == I_ENREC &&
        scan.count == 40, 92);
    for (i = 0; i < scan.count; ++i)
        CHECK((unsigned char)bulk_output[1 + i * 2] == i &&
            !bulk_output[2 + i * 2], 93);
    db_stats(&stats);
    CHECK(!stats.physical_reads && stats.cursor_rows == 80, 94);
    CHECK(db_scan(0, 0, "Z", 1, &scan, bulk_output, 4096, DB_KEYS) == I_ENREC &&
        !scan.count &&
        db_scan(0, 0, 0, 0, &scan, bulk_output, 1, DB_RECORDS) == I_ESIZE &&
        db_scan(0, 0, 0, 0, &scan, bulk_output, 4096, 99) == I_ESIZE, 95);
    CHECK(db_first(0, 0, 0, 0, &scan, 0, 0) == 0, 96);
    scan.mode = DB_RECORDS;
    CHECK(db_more(&scan, bulk_output, 256) == I_OK && scan.count == 2 &&
        !memcmp(bulk_output, bulk_input + I_RECSZ, 256), 97);
    scan.cursor.position = -1;
    CHECK(db_more(&scan, bulk_output, 4096) == I_EINDEX && !scan.count, 98);

    bulk_record(bulk_input, 40);
    bulk_record(bulk_input + I_RECSZ, 41);
    bulk_record(bulk_input + 2 * I_RECSZ, 1);
    bulk_record(bulk_input + 3 * I_RECSZ, 42);
    CHECK(db_batch(0, bulk_input, 4 * I_RECSZ, &count) == I_EDUP && count == 2 &&
        db_info(0, &info) == I_OK && info.records == 42 && info.entries == 42 &&
        db_batch(0, bulk_input, I_RECSZ, &count) == I_EDUP && !count, 99);
    bulk_input[127] = 'Z';
    CHECK(db_put(0, 40, bulk_input, I_RECSZ) == I_OK &&
        find_row(0, 0, bulk_input, 4, bulk_output, I_RECSZ) == 40 &&
        bulk_output[127] == 'Z' && db_del(0, 10) == I_OK &&
        db_batch(0, bulk_input + 3 * I_RECSZ, I_RECSZ, &count) == I_OK &&
        count == 1 && db_info(0, &info) == I_OK && info.slots == 42 &&
        db_probe(0, 0, bulk_input + 3 * I_RECSZ, 4) == 10, 100);
    CHECK(db_first(0, 0, 0, 0, &scan, bulk_output, I_RECSZ) == 0 &&
        scan.cursor.entry_count > info.slots &&
        db_walk(&scan, bulk_output, I_RECSZ) >= 0, 101);
    total = 0;
    rc = db_scan(0, 0, 0, 0, &scan, bulk_output, 4096, DB_RECORDS);
    for (;;)
    {
        total += scan.count;
        if (scan.count)
            memcpy(last_key, bulk_output + (scan.count - 1) * I_RECSZ, 4);
        if (rc != I_OK) break;
        rc = db_more(&scan, bulk_output, 4096);
    }
    CHECK(rc == I_ENREC && total == 42 && !memcmp(last_key, "0042", 4) &&
        db_rebuild(0) == I_OK && db_verify(0, &report) == I_OK, 102);
    CHECK(db_close() == I_OK && bavail() == available + 512 &&
        db_open("BULK.CFG") == I_OK &&
        db_scan(0, 0, 0, 0, &scan, bulk_output, 4096, DB_SLOTS) == I_ENREC &&
        scan.count == 42, 103);
    scan.bytes = 32767;
    CHECK(db_call(DB_NEXT, 0, 0, &scan, sizeof(scan)) == I_ESIZE && !scan.count, 105);
    bytes = DB_BULK_BYTES;
    CHECK(db_call(DB_BIND, 0, 0xffff, &bytes, sizeof(bytes)) == I_ESIZE, 106);
    for (phase = 0; phase < 4; ++phase)
    {
        CHECK(breset(), 107);
        witness = bmalloc(DB_BULK_BYTES);
        memset(bulk_output, 0x5a, 4096);
        CHECK(witness && bwrite(witness, 0, bulk_output, 4096), 108);
        if (!phase)
            rc = db_batch(0, bulk_input, I_RECSZ, &count);
        else if (phase == 1)
        {
            rc = db_more(&scan, bulk_output, 4096);
            count = scan.count;
        }
        else if (phase == 2)
        {
            count = 0;
            rc = db_fetch(0, &count, 1, 0, bulk_output, 4096);
        }
        else
        {
            count = 0;
            rc = db_lookup(0, 0, bulk_input, 1, 4, 0, bulk_output, 4096);
        }
        CHECK(rc == I_EBANK && !count && db_close() == I_EBANK &&
            bsize(witness) == DB_BULK_BYTES && bread(witness, 0, bulk_output, 4096), 109);
        for (i = 0; i < 4096; ++i)
            CHECK(bulk_output[i] == 0x5a, 110);
        CHECK(bfree(witness) && db_open("BULK.CFG") == I_OK &&
            db_scan(0, 0, 0, 0, &scan, bulk_output, 4096, DB_SLOTS) == I_ENREC &&
            scan.count == 42, 111);
    }
    CHECK(find_row(0, 0, bulk_input, 4, bulk_output, I_RECSZ) == 40 &&
        bulk_output[127] == 'Z' && db_close() == I_OK &&
        db_new("BULK.CFG", &schema) == I_OK, 112);
    bulk_record(bulk_input, 0);
    bulk_record(bulk_input + I_RECSZ, 1);
    CHECK(db_batch(0, bulk_input, 2 * I_RECSZ, &count) == I_OK &&
        count == 2 && db_del(0, 0) == I_OK, 113);
    bulk_record(bulk_input, 2);
    bulk_record(bulk_input + I_RECSZ, 3);
    bulk_record(bulk_input + 2 * I_RECSZ, 4);
    memcpy(bulk_input + I_RECSZ + 4, "AAAAAAAAAAA0001", 15);
    CHECK(db_batch(0, bulk_input, 3 * I_RECSZ, &count) == I_EDUP && count == 1 &&
        db_info(0, &info) == I_OK && info.records == 2 && info.slots == 2 &&
        db_probe(0, 0, bulk_input + I_RECSZ, 4) == I_ENREC &&
        db_verify(0, &report) == I_OK && !report.free_slots, 114);
    puts("PASS: bulk tails, projections, partial commits, cache invalidation and stale leases.");
    return I_OK;
}
#endif
