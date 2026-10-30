#include "stdio.h"
#include "string.h"
#include "isamdb.h"

#define T_RECSZ 16

static void make_rec(char *rec, const char *key, const char *value)
{
    int i;

    for (i = 0; i < T_RECSZ; i++)
        rec[i] = 0;
    for (i = 0; i < 4; i++)
        rec[i] = key[i];
    for (i = 0; value[i] && i < T_RECSZ - 5; i++)
        rec[i + 5] = value[i];
}

static int expect_key(const char *rec, const char *key)
{
    int i;

    for (i = 0; i < 4; i++)
        if (rec[i] != key[i])
            return 0;
    return 1;
}

static int setup(void)
{
    int i;

    memset(&g_cfg, 0, sizeof(g_cfg));
    strncpy(g_cfg.dbname, "PKTEST", I_MXNM);
    g_cfg.ntbls = 1;
    strncpy(g_cfg.tbls[0].name, "PKEY", I_MXNM);
    g_cfg.tbls[0].recsz = T_RECSZ;
    g_cfg.tbls[0].nkeys = 2;
    g_cfg.tbls[0].keyoff[0] = 0;
    g_cfg.tbls[0].keysz[0] = 4;
    g_cfg.tbls[0].keyoff[1] = 5;
    g_cfg.tbls[0].keysz[1] = 8;
    for (i = 2; i < I_MXKEY; i++)
    {
        g_cfg.tbls[0].keyoff[i] = 0;
        g_cfg.tbls[0].keysz[i] = 0;
    }
    return i_mktbl(0);
}

#include "tests/CORETST.C"

int main(void)
{
    int fd;
    int phys;
    int rc;
    int deleted_phys;
    char corrupt;
    char rec[T_RECSZ];
    char out[T_RECSZ];
    struct i_cursor cursor;
    struct i_verify report;

    rc = core_checks();
    if (rc != I_OK)
        return rc;
    puts("Persistent multi-key test");
    rc = setup();
    if (rc != I_OK)
    {
        printf("FAIL create rc=%d\r\n", rc);
        return 1;
    }
    fd = open("PKEY.DAT", O_RDONLY);
    if (fd == ERROR)
        return 53;
    close(fd);

    make_rec(rec, "0030", "THIRTY");
    if (i_insrt(0, rec, T_RECSZ) != I_OK)
        return 2;
    make_rec(rec, "0010", "TEN");
    if (i_insrt(0, rec, T_RECSZ) != I_OK)
        return 3;
    make_rec(rec, "0020", "TWENTY");
    if (i_insrt(0, rec, T_RECSZ) != I_OK)
        return 4;

    make_rec(rec, "0020", "DUPLICATE");
    if (i_insrt(0, rec, T_RECSZ) != I_EDUP)
    {
        puts("FAIL duplicate key accepted");
        return 5;
    }

    phys = key_find(0, "0020", out);
    if (phys < 0 || !expect_key(out, "0020"))
    {
        puts("FAIL exact lookup");
        return 6;
    }
    make_rec(rec, "0000", "TWENTY");
    if (idx_find(0, 1, &rec[5], out) < 0 ||
        !expect_key(out, "0020"))
        return 54;

    phys = cur_first(0, &cursor, out);
    if (phys < 0 || !expect_key(out, "0010"))
        return 7;
    phys = cur_next(0, &cursor, out);
    if (phys < 0 || !expect_key(out, "0020"))
        return 8;
    phys = cur_next(0, &cursor, out);
    if (phys < 0 || !expect_key(out, "0030"))
        return 9;
    if (cur_next(0, &cursor, out) != I_ENREC)
        return 10;

    phys = key_find(0, "0020", out);
    make_rec(rec, "0025", "UPDATED");
    if (i_wrphys(0, rec, T_RECSZ, phys) != I_OK)
        return 11;
    if (key_find(0, "0020", out) != I_ENREC)
        return 12;
    if (key_find(0, "0025", out) < 0)
        return 13;
    make_rec(rec, "0000", "TWENTY");
    if (idx_find(0, 1, &rec[5], out) != I_ENREC)
        return 55;
    make_rec(rec, "0000", "UPDATED");
    if (idx_find(0, 1, &rec[5], out) < 0 ||
        !expect_key(out, "0025"))
        return 56;

    phys = key_find(0, "0010", out);
    deleted_phys = phys;
    if (phys < 0 || i_delphys(0, phys) != I_OK)
        return 14;
    if (key_find(0, "0010", out) != I_ENREC)
        return 15;
    make_rec(rec, "0000", "TEN");
    if (idx_find(0, 1, &rec[5], out) != I_ENREC)
        return 57;

    make_rec(rec, "0040", "REUSED");
    if (i_insrt(0, rec, T_RECSZ) != I_OK)
        return 16;
    if (key_find(0, "0040", out) != deleted_phys)
        return 17;
    if (g_cfg.tbls[0].nrecs != 3 || g_cfg.tbls[0].maxrec != 3)
        return 18;

    if (table_verify(0, &report) != I_OK || report.errors != 0 ||
        report.active_records != 3 || report.free_slots != 0)
        return 19;

    g_cfg.tbls[0].nrecs = 99;
    g_cfg.tbls[0].maxrec = 99;
    if (i_cfwr("PKTEST.CFG") != I_OK)
        return 20;
    memset(&g_cfg, 0, sizeof(g_cfg));
    if (i_cfrd("PKTEST.CFG") != I_OK)
        return 21;
    if (g_cfg.tbls[0].nrecs != 3 || g_cfg.tbls[0].maxrec != 3)
        return 22;
    if (key_find(0, "0040", out) < 0)
        return 23;

    fd = open("PKEY.IDX", O_RDWR);
    if (fd == ERROR || lseek(fd, 24L, 0) < 0L)
        return 24;
    corrupt = '9';
    if (write(fd, &corrupt, 1) != 1)
        return 25;
    close(fd);
    if (table_verify(0, &report) != I_EVERIFY || report.errors == 0)
        return 26;
    if (pki_rebuild(0) != I_OK)
        return 27;
    if (table_verify(0, &report) != I_OK)
        return 28;

    if (unlink("PKEY.IDX") != 0)
        return 29;
    if (pki_rebuild(0) != I_OK)
        return 30;
    if (key_find(0, "0025", out) < 0)
        return 31;

    phys = cur_first(0, &cursor, out);
    if (phys < 0 || !expect_key(out, "0025"))
        return 32;
    phys = cur_next(0, &cursor, out);
    if (phys < 0 || !expect_key(out, "0030"))
        return 33;
    phys = cur_next(0, &cursor, out);
    if (phys < 0 || !expect_key(out, "0040"))
        return 34;
    phys = idx_open(0, 1, "UPD", 3, &cursor, out);
    if (phys < 0 || !expect_key(out, "0025") ||
        cur_next(0, &cursor, out) != I_ENREC)
        return 58;

    fd = open("PKEY.MAP", O_RDONLY);
    if (fd != ERROR)
    {
        close(fd);
        return 50;
    }
    fd = open("PKEY.JRN", O_RDONLY);
    if (fd != ERROR)
    {
        close(fd);
        return 59;
    }
    puts("SUCCESS: multi-key insert, update, delete, range, verify, rebuild");
    return 0;
}