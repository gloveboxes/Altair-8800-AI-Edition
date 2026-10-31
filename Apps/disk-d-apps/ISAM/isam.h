#ifndef ISAM_H
#define ISAM_H
#include "isdefs.h"

#define DB_BULK_BYTES 4096
#define DB_RECORDS 0
#define DB_KEYS 1
#define DB_SLOTS 2
#define DB_PHYSICAL (-1)
#define DB_FIELDS 6

struct db_table {
    char name[I_MXNM];
    int size, keys;
    unsigned char offsets[I_MXKEY], widths[I_MXKEY];
    unsigned char tails[I_MXKEY], extra[I_MXKEY];
};

/* Creation input only: no mutable record/index counters in bank 0. */
struct db_schema {
    char name[I_MXNM];
    int tables;
    struct db_table table[I_MXTBL];
};

struct db_info {
    struct db_table definition;
    int records, slots, entries;
};

/* Ranges are concatenated in declaration order; zero fields means full rows. */
struct db_view {
    unsigned char fields;
    unsigned char offsets[DB_FIELDS], sizes[DB_FIELDS];
};

struct db_scan {
    struct i_cursor cursor;
    int mode, bytes, count, width;
    struct db_view view;
};

struct db_pick {
    int index, count, bytes, width;
    struct db_view view;
};

typedef char db_schema_size[(sizeof(struct db_schema) == 126) ? 1 : -1];
typedef char db_cursor_size[(sizeof(struct i_cursor) == 25) ? 1 : -1];
typedef char db_scan_size[(sizeof(struct db_scan) == 46) ? 1 : -1];
typedef char db_pick_size[(sizeof(struct db_pick) == 21) ? 1 : -1];

int db_open(const char *configuration);
int db_new(const char *configuration, const struct db_schema *schema);
int db_close(void);
int db_table(const char *name);
int db_call(int operation, int table, int argument, void *data, unsigned bytes);
int db_send(int operation, int table, int argument, const void *data, unsigned bytes);
int db_first(int table, int key, const char *prefix, int length,
    struct db_scan *scan, char *record, unsigned bytes);
int db_walk(struct db_scan *scan, char *record, unsigned bytes);
int db_scan(int table, int key, const char *prefix, int length,
    struct db_scan *scan, char *rows, int capacity, int mode);
int db_more(struct db_scan *scan, char *rows, int capacity);
/* Inclusive index-entry position in an initialized scan, not a live-row offset. */
int db_page(struct db_scan *scan, int position, char *rows, int capacity);
int db_project(int table, int key, const char *prefix, int length,
    struct db_scan *scan, char *rows, int capacity, const struct db_view *view);
int db_batch(int table, const char *records, int bytes, int *completed);
int db_pick(int table, int index, const void *values, int count, int value_size,
    const struct db_view *view, char *rows, int capacity);
void db_reset(void);
void db_stats(struct i_stats *stats);

/* These operation values also version the counted transport contract. */
#define DB_OPEN 101
#define DB_DEFINE 102
#define DB_CREATE 103
#define DB_TABLE 104
#define DB_INFO 105
#define DB_GET 106
#define DB_PROBE 107
#define DB_READ 108
#define DB_ADD 109
#define DB_PUT 110
#define DB_DELETE 111
#define DB_VERIFY 112
#define DB_REBUILD 113
#define DB_BIND 114
#define DB_BATCH 115
#define DB_SCAN 116
#define DB_NEXT 117
#define DB_RESET 118
#define DB_STATS 119
#define DB_PICK 120
#define DB_BUILD 121
#define DB_FINISH 122
#define DB_APPEND 123
#define DB_PAGE 124

#define db_build(t) db_call(DB_BUILD, (t), 0, 0, 0)
#define db_finish(t) db_call(DB_FINISH, (t), 0, 0, 0)
int db_append(int table, const char *records, int bytes, int *completed);

#define db_fetch(t, s, n, v, r, c) \
    db_pick((t), DB_PHYSICAL, (s), (n), sizeof(int), (v), (r), (c))

#define db_info(t, p) db_call(DB_INFO, (t), 0, (p), sizeof(*(p)))
/* GET consumes a search key at the start of the caller's record buffer. */
#define db_get(t, k, r, n) db_call(DB_GET, (t), (k), (r), (n))
#define db_probe(t, k, p, n) db_send(DB_PROBE, (t), (k), (p), (n))
#define db_read(t, s, r, n) db_call(DB_READ, (t), (s), (r), (n))
#define db_add(t, r, n) db_send(DB_ADD, (t), 0, (r), (n))
#define db_put(t, s, r, n) db_send(DB_PUT, (t), (s), (r), (n))
#define db_del(t, s) db_call(DB_DELETE, (t), (s), 0, 0)
#define db_verify(t, p) db_call(DB_VERIFY, (t), 0, (p), sizeof(*(p)))
#define db_rebuild(t) db_call(DB_REBUILD, (t), 0, 0, 0)

#endif
