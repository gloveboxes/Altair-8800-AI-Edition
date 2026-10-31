#ifndef ISDEFS_H
#define ISDEFS_H

#define I_MXTBL 3
#define I_MXKEY 4
#define I_MXNM 16
#define I_RECSZ 128
#define I_MXKEYLN 15
#define I_CFGBUF 320

#define I_OK 0
#define I_EOPEN -1
#define I_EWRIT -3
#define I_ENTBL -4
#define I_ESIZE -5
#define I_EREAD -6
#define I_ENREC -7
#define I_EUPDT -8
#define I_EDUP -9
#define I_EINDEX -10
#define I_EVERIFY -11
#define I_EBANK -13
#define I_ERECORD -14
#define I_EFULL -15
#define I_EBUILD -16
#define I_KEY_UPPER 128

#define I_DELFLAG (-1)

/* Library-owned cursor state, identical in both banks. */
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
    int physical_reads;
};

#endif
