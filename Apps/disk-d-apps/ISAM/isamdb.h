#ifndef ISAMDB_H
#define ISAMDB_H

#include <fcntl.h>
#include <unistd.h>

/* ============================================================
 * isamdb.h - ISAM Database Library Header
 * ============================================================
 * Shared definitions for ISAM table management and record I/O.
 */

#include "isdefs.h"

/* Table descriptor structure */
struct i_tbl {
    char name[I_MXNM];    /* Table name */
    int recsz;            /* Record size */
    int nkeys;            /* Number of keys */
    unsigned char keyoff[I_MXKEY], keysz[I_MXKEY];
    unsigned char tails[I_MXKEY], extra[I_MXKEY];
    int nrecs;            /* Logical record count */
    int maxrec;           /* Physical high-water mark */
    int idxcnt;           /* Number of index entries */
};

/* Private engine configuration; never mirrored by bank-0 clients. */
struct i_db {
    char dbname[I_MXNM];              /* Database name */
    int ntbls;                        /* Number of tables */
    struct i_tbl tbls[I_MXTBL];       /* Table descriptors */
};

/* The engine is one private bank-1 translation unit. */
#define ISAM_EXTERN extern
#define ERROR -1

ISAM_EXTERN struct i_db g_cfg;

#define I_API static

/* Function declarations */
I_API int i_cfrd(char *fname);                            /* Load config file into g_cfg */
I_API int i_cfwr(char *fname);                            /* Write g_cfg back to disk */
I_API int i_mktbl(int table_name);                      /* Create table data file */
I_API int i_insrt(int tblnam, char *rec, int rsiz);     /* Insert record into table */
I_API int i_rdphys(int tblnam, char *rec, int rnum);    /* Read physical record slot */
I_API int i_wrphys(int tblnam, char *rec, int rsiz, int phys); /* Write physical record slot */
I_API int i_delphys(int tblnam, int phys);              /* Delete physical record slot */
I_API int pki_create(int table_name);
I_API int pki_load(int table_name);
I_API int idx_find(int table_name, int key_index, const char *key,
    char *record);
I_API int pki_rebuild(int table_name); /* Rebuild every configured index */
I_API int idx_open(int table_name, int key_index, const char *prefix,
    int prefix_length, struct i_cursor *cursor, char *record);
I_API int cur_next(int table_name, struct i_cursor *cursor, char *record);
I_API int table_verify(int table_name, struct i_verify *report);
I_API void istat_reset(void);
I_API void iget_stats(struct i_stats *stats);
static int build_indexes(int table, int high_water, int generation);
static int build_pending(int table);

#endif /* ISAMDB_H */