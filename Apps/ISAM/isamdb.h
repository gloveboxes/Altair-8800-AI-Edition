#ifndef ISAMDB_H
#define ISAMDB_H

#include <fcntl.h>
#include <unistd.h>

/* ============================================================
 * isamdb.h - ISAM Database Library Header
 * ============================================================
 * Shared definitions for ISAM table management and record I/O.
 */

/* Maximum limits (match implementation) */
#define I_MXTBL 3     /* Max tables per database */
#define I_MXKEY 4     /* Max keys per table */
#define I_MXNM 16     /* Max name length */
#define I_RECSZ 128   /* Max fixed record size */

#define I_CFGBUF 320

/* Return codes */
#define I_OK 0        /* Success */
#define I_EOPEN -1    /* Cannot open file */
#define I_EWRIT -3    /* Write error */
#define I_ENTBL -4    /* Table not found */
#define I_ESIZE -5    /* Record size mismatch */
#define I_EREAD -6    /* Read error */
#define I_ENREC -7    /* Invalid record number */
#define I_EUPDT -8    /* Update/delete failure */
#define I_EDUP -9     /* Duplicate primary key */
#define I_EINDEX -10  /* Missing or invalid primary index */
#define I_EVERIFY -11 /* Table consistency check failed */
#define I_ERECOV -12  /* Journal recovery failed */

#define I_CRASH_AFTER_JOURNAL 1
#define I_CRASH_AFTER_DATA 2
#define I_CRASH_AFTER_DERIVED 3
#define I_CRASH_AFTER_METADATA 4

/* Delete marker used for lazy delete */
#define I_DELFLAG (-1)

/* Legacy in-memory sparse-index compatibility. */
#ifdef DXISAM_LEGACY_INDEX
#define I_MXKEYLN 12      /* Max key length in index */
#define I_IDXSAMP 20      /* Sample every Nth record for index */
#define I_MXIDX 100       /* Max index entries in memory */

/* Index entry - stores sampled key + physical slot */
struct i_idxent {
    char key[I_MXKEYLN];  /* Key value (zero-padded) */
    int phys;             /* Physical slot number */
};
#else
#define I_MXKEYLN 15      /* Max persistent key length */
#endif

/* Ordered primary-key cursor. Treat fields as library-owned state. */
struct i_cursor {
    int position;
    int entry_count;
    int table_index;
    int key_index;
    int prefix_length;
    char prefix[I_MXKEYLN];
};

struct i_verify {
    int index_entries;
    int active_records;
    int deleted_records;
    int free_slots;
    int errors;
};

struct i_stats {
    int exact_lookups;
    int index_comparisons;
    int cursor_starts;
    int cursor_rows;
    int table_scans;
    int scan_slots;
    int physical_reads;
};

/* Table descriptor structure */
struct i_tbl {
    char name[I_MXNM];    /* Table name */
    char disk;            /* Reserved; files use the current CP/M drive */
    int recsz;            /* Record size */
    int nkeys;            /* Number of keys */
    int keyoff[I_MXKEY];  /* Key field offsets */
    int keysz[I_MXKEY];   /* Key field sizes */
    int nrecs;            /* Logical record count */
    int maxrec;           /* Physical high-water mark */
    int idxcnt;           /* Number of index entries */
    int idxsamp;          /* Sample rate (0 = no index) */
};

/* Database config structure shared by callers */
struct i_db {
    char dbname[I_MXNM];              /* Database name */
    int ntbls;                        /* Number of tables */
    struct i_tbl tbls[I_MXTBL];       /* Table descriptors */
};

#define DX_EXTERN extern
#define ERROR -1

DX_EXTERN struct i_db g_cfg;

/* Function declarations */
int i_cfrd(char *fname);                            /* Load config file into g_cfg */
int i_cfwr(char *fname);                            /* Write g_cfg back to disk */
int i_mktbl(char *table_name);                      /* Create table data file */
int i_insrt(char *tblnam, char *rec, int rsiz);     /* Insert record into table */
int i_rdrec(char *tblnam, char *rec, int rnum);     /* Read logical record */
int i_rdphys(char *tblnam, char *rec, int rnum);    /* Read physical record slot */
int i_wrphys(char *tblnam, char *rec, int rsiz, int phys); /* Write physical record slot */
int i_delphys(char *tblnam, int phys);              /* Delete physical record slot */
int i_uprec(char *tblnam, char *rec, int rsiz, int rnum);  /* Update logical record */
int i_delrec(char *tblnam, int rnum);               /* Lazy delete logical record */
#ifdef DXISAM_LEGACY_INDEX
int ixbld();  /* Build sparse index for table */
int ixsrch(); /* Binary search index for key */
int ixlook(); /* High-level indexed lookup */
int ixins();  /* Insert/update single index entry */
int ixdel();  /* Remove index entry by physical slot */
#endif
int pki_create(const char *table_name);
int pki_load(const char *table_name);
int key_find(const char *table_name, const char *key, char *record);
int idx_find(const char *table_name, int key_index, const char *key,
    char *record);
int key_add(const char *table_name, const char *record, int physical_slot);
int key_remove(const char *table_name, const char *record, int physical_slot);
int key_update(const char *table_name, const char *old_record,
    const char *new_record, int physical_slot);
int pki_rebuild(const char *table_name); /* Rebuild every configured index */
int cur_first(const char *table_name, struct i_cursor *cursor, char *record);
int idx_open(const char *table_name, int key_index, const char *prefix,
    int prefix_length, struct i_cursor *cursor, char *record);
int cur_next(const char *table_name, struct i_cursor *cursor, char *record);
int table_verify(const char *table_name, struct i_verify *report);
void istat_reset(void);
void iget_stats(struct i_stats *stats);
void i_test_crash_after(int phase);

#endif /* ISAMDB_H */