#include "stdlib.h"

static int audit_setup(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    strcpy(g_cfg.dbname, "AUDIT");
    g_cfg.ntbls = 1;
    strcpy(g_cfg.tbls[0].name, "AUDIT");
    g_cfg.tbls[0].recsz = T_RECSZ;
    g_cfg.tbls[0].nkeys = 2;
    g_cfg.tbls[0].keysz[0] = 4;
    g_cfg.tbls[0].keyoff[1] = 5;
    g_cfg.tbls[0].keysz[1] = 8;
    return i_mktbl(0);
}

static int audit_pair(void)
{
    char record[T_RECSZ];
    if (audit_setup() != I_OK)
        return 121;
    make_rec(record, "0001", "ALPHA");
    if (i_insrt(0, record, T_RECSZ) != I_OK)
        return 122;
    make_rec(record, "0002", "BETA");
    return i_insrt(0, record, T_RECSZ) == I_OK ? I_OK : 123;
}

static int audit_rejections(void)
{
    char old[T_RECSZ], record[T_RECSZ], output[T_RECSZ];
    struct i_verify report;
    int rc;

    if (audit_pair() != I_OK || i_delphys(0, 0) != I_OK)
        return 124;
    make_rec(record, "0003", "BETA");
    outp(5, 30);
    rc = i_insrt(0, record, T_RECSZ);
    outp(5, 158);
    if (rc != I_EDUP || g_cfg.tbls[0].nrecs != 1 ||
        g_cfg.tbls[0].maxrec != 2 ||
        table_verify(0, &report) != I_OK || report.free_slots != 1)
        return 125;

    if (audit_pair() != I_OK)
        return 126;
    make_rec(old, "0001", "ALPHA");
    make_rec(record, "0003", "BETA");
    outp(5, 31);
    rc = i_wrphys(0, record, T_RECSZ, 0);
    outp(5, 159);
    if (rc != I_EDUP || key_find(0, old, output) != 0 ||
        memcmp(old, output, T_RECSZ) ||
        key_find(0, record, 0) != I_ENREC ||
        table_verify(0, &report) != I_OK)
        return 127;
    if (key_update(0, old, record, 0) != I_EDUP ||
        key_find(0, old, output) != 0 ||
        table_verify(0, &report) != I_OK)
        return 128;
    return I_OK;
}

static int audit_tombstones(void)
{
    char record[T_RECSZ], output[T_RECSZ];
    struct i_cursor cursor;
    struct i_verify report;

    if (audit_pair() != I_OK || i_delphys(0, 0) != I_OK)
        return 129;
    make_rec(record, "0001", "ALPHA");
    if (i_wrphys(0, record, T_RECSZ, 1) != I_OK ||
        g_cfg.tbls[0].nrecs != 1 || g_cfg.tbls[0].maxrec != 2 ||
        g_cfg.tbls[0].idxcnt != 1 ||
        key_find(0, record, output) != 1 ||
        table_verify(0, &report) != I_OK ||
        cur_first(0, &cursor, output) != 1 ||
        cur_next(0, &cursor, output) != I_ENREC)
        return 130;
    if (audit_pair() != I_OK || i_delphys(0, 0) != I_OK)
        return 131;
    make_rec(record, "0001", "GAMMA");
    if (i_insrt(0, record, T_RECSZ) != I_OK ||
        g_cfg.tbls[0].idxcnt != 2 ||
        table_verify(0, &report) != I_OK ||
        i_cfwr("AUDIT.CFG") != I_OK)
        return 132;
    outp(5, 32);
    if (i_cfrd("AUDIT.CFG") != I_OK)
        return 133;
    outp(5, 160);
    if (g_cfg.tbls[0].idxcnt != 2 ||
        table_verify(0, &report) != I_OK ||
        key_find(0, record, output) != 0)
        return 134;
    if (i_mktbl(0) != I_OK || g_cfg.tbls[0].nrecs ||
        g_cfg.tbls[0].maxrec || g_cfg.tbls[0].idxcnt ||
        cur_first(0, &cursor, output) != I_ENREC)
        return 135;
    return I_OK;
}

static int audit_rotation(void)
{
    char record[T_RECSZ], output[T_RECSZ];
    struct i_verify report;
    struct i_cursor cursor;

    if (audit_pair() != I_OK)
        return 142;
    make_rec(record, "0003", "GAMMA");
    if (i_insrt(0, record, T_RECSZ) != I_OK)
        return 143;
    make_rec(record, "0004", "DELTA");
    if (i_wrphys(0, record, T_RECSZ, 1) != I_OK ||
        key_find(0, record, output) != 1 ||
        table_verify(0, &report) != I_OK)
        return 144;
    make_rec(record, "0000", "AARDVARK");
    if (i_wrphys(0, record, T_RECSZ, 1) != I_OK ||
        cur_first(0, &cursor, output) != 1 ||
        cur_next(0, &cursor, output) != 0 ||
        cur_next(0, &cursor, output) != 2 ||
        cur_next(0, &cursor, output) != I_ENREC ||
        table_verify(0, &report) != I_OK)
        return 145;
    make_rec(record, "0005", "ZZZ");
    if (i_wrphys(0, record, T_RECSZ, 0) != I_OK ||
        i_delphys(0, 0) != I_OK ||
        i_wrphys(0, record, T_RECSZ, 1) != I_OK ||
        g_cfg.tbls[0].idxcnt != 2 ||
        key_find(0, record, output) != 1 ||
        idx_find(0, 1, record + 5, output) != 1 ||
        table_verify(0, &report) != I_OK || report.free_slots != 1)
        return 146;
    return I_OK;
}

static int audit_errors(void)
{
    char record[T_RECSZ], output[T_RECSZ], slot[2];
    struct i_cursor cursor;
    struct i_verify report;
    int fd, rc;

    if (audit_pair() != I_OK)
        return 136;
    if (i_rdrec("ABSENT", output, 0) != I_ENTBL ||
        i_uprec("ABSENT", output, T_RECSZ, 0) != I_ENTBL ||
        i_delrec("ABSENT", 0) != I_ENTBL)
        return 137;
    make_rec(record, "0003", "GAMMA");
    record[0] = I_DELFLAG;
    if (i_insrt(0, record, T_RECSZ) != I_ERECORD ||
        i_wrphys(0, record, T_RECSZ, 0) != I_ERECORD ||
        table_verify(0, &report) != I_OK)
        return 138;
    if (i_delphys(0, 0) != I_OK)
        return 164;
    slot[0] = 0xfe;
    slot[1] = 0xff;
    fd = open("AUDIT.DAT", O_RDWR);
    if (fd == ERROR || lseek(fd, 1L, 0) < 0L ||
        write(fd, slot, 2) != 2 || close(fd) != 0 ||
        table_verify(0, &report) != I_EVERIFY)
        return 165;
    make_rec(record, "0003", "GAMMA");
    outp(5, 35);
    rc = i_insrt(0, record, T_RECSZ);
    outp(5, 163);
    if (rc != I_EINDEX || g_cfg.tbls[0].nrecs != 1 ||
        audit_pair() != I_OK)
        return 166;
    slot[0] = 0xfe;
    slot[1] = 0x7f;
    fd = open("AUDIT.IDX", O_RDWR);
    if (fd == ERROR || lseek(fd, 28L, 0) < 0L ||
        write(fd, slot, 2) != 2 || close(fd) != 0)
        return 139;
    if (table_verify(0, &report) != I_EVERIFY)
        return 140;
    if (key_find(0, "0001", 0) != I_EINDEX ||
        cur_first(0, &cursor, 0) != I_EINDEX)
        return 141;
    return I_OK;
}

static void audit_checksum(char *header)
{
    unsigned int checksum;
    int i;
    checksum = 0;
    for (i = 0; i < 20; ++i)
        checksum += (unsigned char)header[i];
    header[20] = checksum;
    header[21] = checksum >> 8;
}

static int audit_limits(void)
{
    char header[24], base[24], record[T_RECSZ];
    int fd, field, rc;

    if (audit_pair() != I_OK)
        return 147;
    fd = open("AUDIT.IDX", O_RDONLY);
    if (fd == ERROR || read(fd, base, sizeof(base)) != sizeof(base) ||
        close(fd) != 0)
        return 148;
    make_rec(record, "0003", "GAMMA");
    for (field = 10; field <= 16; field += 2)
    {
        if (pki_create(0) != I_OK)
            return 149;
        memcpy(header, base, sizeof(header));
        header[field] = 0;
        header[field + 1] = 0x80;
        audit_checksum(header);
        fd = open("AUDIT.IDX", O_RDWR);
        if (fd == ERROR || write(fd, header, sizeof(header)) != sizeof(header) ||
            close(fd) != 0 ||
            i_insrt(0, record, T_RECSZ) != I_EINDEX)
            return 150;
    }
    if (pki_create(0) != I_OK)
        return 151;
    memcpy(header, base, sizeof(header));
    for (field = 10; field <= 14; field += 2)
    {
        header[field] = 0xff;
        header[field + 1] = 0x7f;
    }
    audit_checksum(header);
    fd = open("AUDIT.IDX", O_RDWR);
    if (fd == ERROR || write(fd, header, sizeof(header)) != sizeof(header) ||
        close(fd) != 0)
        return 152;
    outp(5, 34);
    rc = i_insrt(0, record, T_RECSZ);
    outp(5, 162);
    return rc == I_EFULL ? I_OK : 153;
}

static int audit_offsets(void)
{
    char record[T_RECSZ], output[T_RECSZ], header[24], entries[119];
    char *entry;
    struct i_cursor cursor;
    struct i_verify report;
    int fd, first, count, i, number;

    if (audit_setup() != I_OK)
        return 154;
    g_cfg.tbls[0].nkeys = 1;
    g_cfg.tbls[0].keysz[0] = 15;
    if (i_mktbl(0) != I_OK)
        return 155;
    memset(record, 0, sizeof(record));
    memcpy(record, "0000ABCDEFGHIJK", 15);
    if (i_insrt(0, record, T_RECSZ) != I_OK)
        return 156;
    memcpy(record, "3999ABCDEFGHIJK", 15);
    if (i_insrt(0, record, T_RECSZ) != I_OK ||
        i_cfwr("AUDIT.CFG") != I_OK || pki_create(0) != I_OK)
        return 157;
    fd = open("AUDIT.IDX", O_RDWR);
    if (fd == ERROR || read(fd, header, sizeof(header)) != sizeof(header))
        return 158;
    header[10] = 0xa0;
    header[11] = 0x0f;
    header[12] = header[14] = 2;
    audit_checksum(header);
    if (lseek(fd, 0L, 0) < 0L || write(fd, header, sizeof(header)) != sizeof(header))
        return 159;
    for (first = 0; first < 4000; first += count)
    {
        count = 4000 - first;
        if (count > 7)
            count = 7;
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
            if (number == 0 || number == 3999)
            {
                entry[15] = number ? 1 : 0;
                entry[16] = 0;
            }
        }
        if (write(fd, entries, count * 17) != count * 17)
            return 160;
    }
    if (close(fd) != 0 || i_cfrd("AUDIT.CFG") != I_OK ||
        g_cfg.tbls[0].idxcnt != 4000 ||
        key_find(0, record, output) != 1 ||
        memcmp(record, output, T_RECSZ) ||
        key_find(0, "3853ABCDEFGHIJK", 0) != I_ENREC ||
        key_find(0, "3854ABCDEFGHIJK", 0) != I_ENREC ||
        idx_open(0, 0, "399", 3, &cursor, output) != 1 ||
        cur_next(0, &cursor, output) != I_ENREC ||
        table_verify(0, &report) != I_OK)
        return 161;
    puts("PASS: 4000-entry index crosses 64 KiB with fast and long offsets.");
    return I_OK;
}

static int core_checks(void)
{
    int rc;
    rc = audit_rejections();
    if (rc == I_OK)
        rc = audit_tombstones();
    if (rc == I_OK)
        rc = audit_rotation();
    if (rc == I_OK)
        rc = audit_errors();
    if (rc == I_OK)
        rc = audit_limits();
    if (rc == I_OK)
        rc = audit_offsets();
    if (rc == I_OK)
    {
        if (pki_create(0) != I_OK || unlink("AUDIT.CFG") != 0 ||
            unlink("AUDIT.DAT") != 0 || unlink("AUDIT.IDX") != 0 ||
            unlink("AUDIT.IX1") != 0)
            rc = 162;
    }
    if (rc == I_OK)
        puts("PASS: mutation preflight, tombstone counts, reopen and error propagation.");
    else
        printf("FAIL: ISAM core audit rc=%d\r\n", rc);
    return rc;
}
