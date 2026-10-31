#include "stdio.h"
#include "string.h"
#include "isamdb.h"

struct i_db g_cfg;
struct i_stats i_runtime_stats;

static int valid_config(const struct i_db *config)
{
    int table, key, first, extra, prior;
    const struct i_tbl *entry;
    if (config->ntbls < 1 || config->ntbls > I_MXTBL ||
        !config->dbname[0] || !memchr(config->dbname, 0, I_MXNM))
        return I_ESIZE;
    for (table = 0; table < config->ntbls; ++table)
    {
        entry = &config->tbls[table];
        if (!entry->name[0] || !memchr(entry->name, 0, I_MXNM) ||
            entry->recsz < 3 || entry->recsz > I_RECSZ ||
            entry->nkeys < 1 || entry->nkeys > I_MXKEY)
            return I_ESIZE;
        for (prior = 0; prior < table; ++prior)
            if (!strcmp(entry->name, config->tbls[prior].name))
                return I_ESIZE;
        for (key = 0; key < entry->nkeys; ++key)
        {
            extra = entry->extra[key];
            first = entry->keysz[key] - extra;
            if (entry->keysz[key] < 1 || entry->keysz[key] > I_MXKEYLN ||
                first < 1 ||
                (entry->keyoff[key] & 127) > entry->recsz - first ||
                extra > entry->recsz ||
                (extra && entry->tails[key] > entry->recsz - extra))
                return I_ESIZE;
        }
    }
    return I_OK;
}

static void make_index_key(int table, int key, const char *record, char *out)
{
    const struct i_tbl *entry;
    int first, extra, i;
    entry = &g_cfg.tbls[table];
    extra = entry->extra[key];
    first = entry->keysz[key] - extra;
    memcpy(out, record + (entry->keyoff[key] & 127), first);
    if (extra)
        memcpy(out + first, record + entry->tails[key], extra);
    if (entry->keyoff[key] & I_KEY_UPPER)
        for (i = 0; i < first; ++i)
            if (out[i] >= 'a' && out[i] <= 'z')
                out[i] -= 'a' - 'A';
}

#define stats_add(counter) do { if (*(counter) < 32767) ++*(counter); } while (0)

void istat_reset(void)
{
    memset(&i_runtime_stats, 0, sizeof(i_runtime_stats));
}

void iget_stats(struct i_stats *stats)
{
    memcpy(stats, &i_runtime_stats, sizeof(i_runtime_stats));
}

int i_cfwr(char *fname)
{
    int fd, rc;
    rc = valid_config(&g_cfg);
    if (rc != I_OK)
        return rc;
    fd = open(fname, O_CREAT | O_TRUNC | O_WRONLY, 0);
    if (fd == ERROR)
        return I_EOPEN;
    rc = I_OK;
    if (write(fd, "DXC4", 4) != 4 ||
        write(fd, &g_cfg, sizeof(g_cfg)) != sizeof(g_cfg) || fsync(fd) != 0)
        rc = I_EWRIT;
    if (close(fd) != 0 && rc == I_OK)
        rc = I_EWRIT;
    return rc;
}

int i_cfrd(char *fname)
{
    int fd, rc, table;
    char magic[4];
    fd = open(fname, O_RDONLY);
    if (fd == ERROR)
        return I_EOPEN;
    rc = I_OK;
    if (read(fd, magic, 4) != 4 || memcmp(magic, "DXC4", 4) ||
        read(fd, &g_cfg, sizeof(g_cfg)) != sizeof(g_cfg))
        rc = I_ESIZE;
    if (close(fd) != 0 && rc == I_OK)
        rc = I_EREAD;
    if (rc == I_OK)
        rc = valid_config(&g_cfg);
    if (rc != I_OK)
    {
        memset(&g_cfg, 0, sizeof(g_cfg));
        return rc;
    }
    for (table = 0; table < g_cfg.ntbls; ++table)
    {
        rc = pki_load(table);
        if (rc != I_OK)
            return rc;
    }
    return I_OK;
}
