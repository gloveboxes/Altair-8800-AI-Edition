#include "stdio.h"
#include "string.h"
#include "isamdb.h"

struct i_db g_cfg;
static char config_buffer[I_CFGBUF];
struct i_stats i_runtime_stats;

static void stats_add(int *counter, int amount)
{
    if (amount > 32767 - *counter)
        *counter = 32767;
    else
        *counter += amount;
}

void istat_reset(void)
{
    memset(&i_runtime_stats, 0, sizeof(i_runtime_stats));
}

void iget_stats(struct i_stats *stats)
{
    memcpy(stats, &i_runtime_stats, sizeof(i_runtime_stats));
}

char *i_wrint(char *p, int val);
char *i_rdint(char *p, char *pend, int *val);

int i_cfwr(char *fname)
{
    int fd;
    int i, j, len;
    char *buf;
    char num[10];
    char *p;
    
    buf = config_buffer;
    fd = open(fname, O_CREAT | O_TRUNC | O_WRONLY, 0);
    if (fd == ERROR)
        return I_EOPEN;
    
    p = buf;
    
    /* Write database name */
    i = 0;
    while (g_cfg.dbname[i] && i < I_MXNM)
        *p++ = g_cfg.dbname[i++];
    *p++ = '\n';
    
    /* Write number of tables */
    i = g_cfg.ntbls;
    j = 0;
    if (i == 0)
        num[j++] = '0';
    else
    {
        len = 0;
        while (i > 0)
        {
            num[len++] = (i % 10) + '0';
            i = i / 10;
        }
        for (j = len - 1; j >= 0; j--)
            *p++ = num[j];
    }
    *p++ = '\n';
    
    /* Write each table */
    for (i = 0; i < g_cfg.ntbls && i < I_MXTBL; i++)
    {
        /* Table name */
        j = 0;
        while (g_cfg.tbls[i].name[j] && j < I_MXNM)
            *p++ = g_cfg.tbls[i].name[j++];
        *p++ = '\n';
        
        /* Disk */
        *p++ = g_cfg.tbls[i].disk;
        *p++ = '\n';
        
        /* Write integers with helper function */
        p = i_wrint(p, g_cfg.tbls[i].recsz);
        p = i_wrint(p, g_cfg.tbls[i].nkeys);
        p = i_wrint(p, g_cfg.tbls[i].nrecs);
        p = i_wrint(p, g_cfg.tbls[i].maxrec);
        
        /* Key offsets */
        for (j = 0; j < g_cfg.tbls[i].nkeys && j < I_MXKEY; j++)
            p = i_wrint(p, g_cfg.tbls[i].keyoff[j]);
        
        /* Key sizes */
        for (j = 0; j < g_cfg.tbls[i].nkeys && j < I_MXKEY; j++)
            p = i_wrint(p, g_cfg.tbls[i].keysz[j]);
    }
    
    /* Write buffer to file */
    len = p - buf;
    if (write(fd, buf, len) != len)
    {
        close(fd);
        return I_EWRIT;
    }
    
    close(fd);
    return I_OK;
}

/* Helper: write integer to buffer as decimal string with newline */
char *i_wrint(char *p, int val)
{
    int i, len;
    char num[10];
    
    if (val == 0)
    {
        *p++ = '0';
        *p++ = '\n';
        return p;
    }
    
    len = 0;
    while (val > 0)
    {
        num[len++] = (val % 10) + '0';
        val = val / 10;
    }
    
    for (i = len - 1; i >= 0; i--)
        *p++ = num[i];
    *p++ = '\n';
    
    return p;
}

int i_cfrd(char *fname)
{
    int fd, i, t, nbytes, rc;
    char *p;
    char *pend;
    char *buf;
    
    buf = config_buffer;
    fd = open(fname, O_RDONLY);
    if (fd == ERROR)
        return I_EOPEN;
    
    /* Read file into buffer */
    nbytes = read(fd, buf, I_CFGBUF);
    if (nbytes <= 0)
    {
        close(fd);
        return I_EOPEN;
    }
    
    close(fd);
    
    p = buf;
    pend = buf + nbytes;
    
    /* Read dbname */
    i = 0;
    while (p < pend && *p != '\n' && i < I_MXNM - 1)
    {
        g_cfg.dbname[i] = *p;
        i++;
        p++;
    }
    g_cfg.dbname[i] = 0;
    if (p < pend && *p == '\n')
        p++;
    
    /* Read ntbls */
    g_cfg.ntbls = 0;
    while (p < pend && *p >= '0' && *p <= '9')
    {
        g_cfg.ntbls = g_cfg.ntbls * 10 + (*p - '0');
        p++;
    }
    if (p < pend && *p == '\n')
        p++;
    
    /* Read each table */
    for (t = 0; t < g_cfg.ntbls && t < I_MXTBL; t++)
    {
        /* Table name */
        i = 0;
        while (p < pend && *p != '\n' && i < I_MXNM - 1)
        {
            g_cfg.tbls[t].name[i] = *p;
            i++;
            p++;
        }
        g_cfg.tbls[t].name[i] = 0;
        if (p < pend && *p == '\n')
            p++;
        
        /* Disk */
        if (p >= pend)
            break;
        g_cfg.tbls[t].disk = *p++;
        if (p < pend && *p == '\n')
            p++;
        
        /* Record size, keys, counts */
        p = i_rdint(p, pend, &g_cfg.tbls[t].recsz);
        p = i_rdint(p, pend, &g_cfg.tbls[t].nkeys);
        p = i_rdint(p, pend, &g_cfg.tbls[t].nrecs);
        p = i_rdint(p, pend, &g_cfg.tbls[t].maxrec);
        
        /* Key offsets */
        for (i = 0; i < g_cfg.tbls[t].nkeys && i < I_MXKEY; i++)
            p = i_rdint(p, pend, &g_cfg.tbls[t].keyoff[i]);
        
        /* Key sizes */
        for (i = 0; i < g_cfg.tbls[t].nkeys && i < I_MXKEY; i++)
            p = i_rdint(p, pend, &g_cfg.tbls[t].keysz[i]);
    }

    for (t = 0; t < g_cfg.ntbls && t < I_MXTBL; t++)
    {
        rc = pki_load(g_cfg.tbls[t].name);
        if (rc != I_OK)
            return rc;
    }
    
    return I_OK;
}

char *i_rdint(char *p, char *pend, int *val)
{
    *val = 0;
    while (p < pend && *p >= '0' && *p <= '9')
    {
        *val = (*val * 10) + (*p - '0');
        p++;
    }
    if (p < pend && *p == '\n')
        p++;
    return p;
}
