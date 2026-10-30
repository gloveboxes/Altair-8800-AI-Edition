static void checksum(char *header)
{
    unsigned int sum;
    int i;
    sum = 0;
    for (i = 0; i < 20; ++i)
        sum += (unsigned char)header[i];
    header[20] = sum;
    header[21] = sum >> 8;
}

static int audit_pair(void)
{
    char record[16];
    CHECK(db_close() == I_OK, 121);
    test_schema("AUDIT", 16, 4);
    schema.table[0].keys = 2;
    schema.table[0].offsets[1] = 5;
    schema.table[0].widths[1] = 8;
    CHECK(db_new("AUDIT.CFG", &schema) == I_OK, 122);
    make_rec(record, "0001", "ALPHA");
    CHECK(db_add(0, record, 16) == I_OK, 123);
    make_rec(record, "0002", "BETA");
    CHECK(db_add(0, record, 16) == I_OK, 124);
    return I_OK;
}

static int native_audit(void)
{
    struct i_verify report;
    struct db_info info;
    struct db_scan scan;
    char record[16], output[16], link[2], base[24], header[24], entries[119];
    char *entry;
    int fd, rc, field, first, count, i, number;
    CHECK(audit_pair() == I_OK && db_del(0, 0) == I_OK, 125);
    make_rec(record, "0003", "BETA");
    outp(5, 30);
    rc = db_add(0, record, 16);
    outp(5, 158);
    CHECK(rc == I_EDUP && db_info(0, &info) == I_OK &&
        info.records == 1 && info.slots == 2 &&
        db_verify(0, &report) == I_OK && report.free_slots == 1, 126);
    CHECK(audit_pair() == I_OK, 127);
    outp(5, 31);
    rc = db_put(0, 0, record, 16);
    outp(5, 159);
    CHECK(rc == I_EDUP && db_probe(0, 0, "0001", 4) == 0 &&
        db_probe(0, 0, "0003", 4) == I_ENREC &&
        db_verify(0, &report) == I_OK, 128);
    make_rec(record, "0003", "GAMMA");
    CHECK(db_add(0, record, 16) == I_OK, 129);
    make_rec(record, "0004", "DELTA");
    CHECK(db_put(0, 1, record, 16) == I_OK, 130);
    make_rec(record, "0000", "AARDVARK");
    CHECK(db_put(0, 1, record, 16) == I_OK &&
        db_first(0, 0, 0, 0, &scan, output, 16) == 1 &&
        db_walk(&scan, output, 16) == 0 &&
        db_walk(&scan, output, 16) == 2 &&
        db_walk(&scan, output, 16) == I_ENREC, 131);
    make_rec(record, "0005", "ZZZ");
    CHECK(db_put(0, 0, record, 16) == I_OK && db_del(0, 0) == I_OK &&
        db_put(0, 1, record, 16) == I_OK &&
        db_info(0, &info) == I_OK && info.entries == 2 &&
        db_verify(0, &report) == I_OK && report.free_slots == 1, 132);
    CHECK(audit_pair() == I_OK && db_del(0, 0) == I_OK, 133);
    make_rec(record, "0001", "GAMMA");
    CHECK(db_add(0, record, 16) == I_OK && db_close() == I_OK, 134);
    outp(5, 32);
    rc = db_open("AUDIT.CFG");
    outp(5, 160);
    CHECK(rc == I_OK && db_info(0, &info) == I_OK &&
        info.entries == 2 && db_verify(0, &report) == I_OK, 135);
    record[0] = 0xff;
    CHECK(db_add(0, record, 16) == I_ERECORD &&
        db_put(0, 0, record, 16) == I_ERECORD &&
        db_read(-1, 0, output, 16) == I_ENTBL &&
        db_put(-1, 0, record, 16) == I_ENTBL &&
        db_del(-1, 0) == I_ENTBL, 136);
    CHECK(db_del(0, 0) == I_OK, 137);
    link[0] = 0xfe; link[1] = 0xff;
    fd = open("AUDIT.DAT", O_RDWR);
    CHECK(fd >= 0 && lseek(fd, 1L, 0) >= 0L &&
        write(fd, link, 2) == 2 && close(fd) == 0 &&
        db_verify(0, &report) == I_EVERIFY, 138);
    make_rec(record, "0003", "GAMMA");
    outp(5, 35);
    rc = db_add(0, record, 16);
    outp(5, 163);
    CHECK(rc == I_EINDEX && audit_pair() == I_OK, 139);
    link[0] = 0xfe; link[1] = 0x7f;
    fd = open("AUDIT.IDX", O_RDWR);
    CHECK(fd >= 0 && lseek(fd, 28L, 0) >= 0L &&
        write(fd, link, 2) == 2 && close(fd) == 0 &&
        db_verify(0, &report) == I_EVERIFY &&
        db_probe(0, 0, "0001", 4) == I_EINDEX, 140);
    CHECK(db_close() == I_OK, 141);
    test_schema("AUDIT", 16, 4);
    CHECK(db_new("AUDIT.CFG", &schema) == I_OK, 141);
    make_rec(record, "0001", "ALPHA");
    CHECK(db_add(0, record, 16) == I_OK, 141);
    make_rec(record, "0002", "BETA");
    CHECK(db_add(0, record, 16) == I_OK && db_close() == I_OK, 141);
    fd = open("AUDIT.IDX", O_RDONLY);
    CHECK(fd >= 0 && read(fd, base, 24) == 24 && close(fd) == 0, 142);
    for (field = 10; field <= 16; field += 2)
    {
        memcpy(header, base, 24);
        header[field] = 0; header[field + 1] = 0x80;
        checksum(header);
        fd = open("AUDIT.IDX", O_RDWR);
        CHECK(fd >= 0 && write(fd, header, 24) == 24 && close(fd) == 0 &&
            db_open("AUDIT.CFG") == I_EINDEX, 143);
    }
    memcpy(header, base, 24);
    for (field = 10; field <= 14; field += 2)
    {
        header[field] = 0xff; header[field + 1] = 0x7f;
    }
    checksum(header);
    fd = open("AUDIT.IDX", O_RDWR);
    CHECK(fd >= 0 && write(fd, header, 24) == 24 && close(fd) == 0 &&
        db_open("AUDIT.CFG") == I_OK, 144);
    make_rec(record, "0003", "GAMMA");
    outp(5, 34);
    rc = db_add(0, record, 16);
    outp(5, 162);
    CHECK(rc == I_EFULL && db_close() == I_OK, 145);

    test_schema("AUDIT", 16, 15);
    CHECK(db_new("AUDIT.CFG", &schema) == I_OK, 146);
    memset(record, 0, 16);
    memcpy(record, "0000ABCDEFGHIJK", 15);
    CHECK(db_add(0, record, 16) == I_OK, 147);
    memcpy(record, "3999ABCDEFGHIJK", 15);
    CHECK(db_add(0, record, 16) == I_OK && db_close() == I_OK, 148);
    fd = open("AUDIT.IDX", O_RDWR);
    CHECK(fd >= 0 && read(fd, header, 24) == 24, 149);
    header[10] = 0xa0; header[11] = 0x0f;
    header[12] = header[14] = 2;
    checksum(header);
    CHECK(lseek(fd, 0L, 0) >= 0L && write(fd, header, 24) == 24, 150);
    for (first = 0; first < 4000; first += count)
    {
        count = 4000 - first;
        if (count > 7) count = 7;
        for (i = 0; i < count; ++i)
        {
            entry = entries + i * 17;
            number = first + i;
            memcpy(entry, "0000ABCDEFGHIJK", 15);
            entry[0] = '0' + number / 1000;
            entry[1] = '0' + number / 100 % 10;
            entry[2] = '0' + number / 10 % 10;
            entry[3] = '0' + number % 10;
            entry[15] = entry[16] = 0xff;
            if (!number || number == 3999)
            {
                entry[15] = number ? 1 : 0;
                entry[16] = 0;
            }
        }
        CHECK(write(fd, entries, count * 17) == count * 17, 151);
    }
    CHECK(close(fd) == 0 && db_open("AUDIT.CFG") == I_OK &&
        db_info(0, &info) == I_OK && info.entries == 4000 &&
        find_row(0, 0, record, 15, output, 16) == 1 &&
        !memcmp(record, output, 16) &&
        db_probe(0, 0, "3853ABCDEFGHIJK", 15) == I_ENREC &&
        db_probe(0, 0, "3854ABCDEFGHIJK", 15) == I_ENREC &&
        db_first(0, 0, "399", 3, &scan, output, 16) == 1 &&
        db_walk(&scan, output, 16) == I_ENREC &&
        db_verify(0, &report) == I_OK && db_close() == I_OK, 152);
    puts("PASS: 4000-entry index crosses 64 KiB with fast and long offsets.");
    CHECK(unlink("AUDIT.CFG") == 0 && unlink("AUDIT.DAT") == 0 &&
        unlink("AUDIT.IDX") == 0 && unlink("AUDIT.IX1") == 0, 153);
    return I_OK;
}
