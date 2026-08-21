# DXISAM ISAM Library

## Overview

DXISAM is a fixed-record Indexed Sequential Access Method library for dcc on CP/M 2.2. It uses DCCRTL random file I/O and maintains up to four complete, sorted, persistent indexes per table.

Key characteristics:

- Fixed-size records stored in `TABLE.DAT` on the current CP/M drive.
- Sorted key-zero entries stored in `TABLE.IDX`; secondary keys use `TABLE.IX1` through `TABLE.IX3`.
- Constant-time deleted-slot links stored inside deleted `.DAT` records.
- Optional single-operation undo journals stored in `TABLE.JRN` on the current drive.
- Exact-size byte records with 32-bit file offsets; no application-level CP/M sector padding.
- Text configuration (`*.CFG`) that describes databases and tables via the global `g_cfg` structure.
- Single-threaded, no dynamic allocation, designed for ≤200 KB data sets under CP/M.
- Lazy delete support that marks records and allows future inserts to reuse freed slots.
- Unique-key enforcement, exact lookup, prefix-range traversal, reopen, and all-index rebuild.
- Checksummed durable counts, high-water slot, free-list head, and mutation generation.
- Automatic rollback of interrupted mutations and completion of committed mutations on reopen.

## How ISAM Works

Records remain in stable physical slots while each index file stores fixed-size `{key, physical-slot}` entries in key order. Binary search finds an exact key without requiring the data file itself to be physically sorted. Inserts, physical updates, and deletes maintain every configured index automatically.

Delete marks the `.DAT` record and replaces its physical slot with `0xFFFF` in every index. Keys remain as sorted tombstones, so deletion requires no index tail rewrite. Lookup and cursors skip tombstones, reinserting the same keys reuses their entries, and `pki_rebuild` compacts all indexes.

The version-3 `.IDX` header for key zero is the authority for active/index counts, physical high-water slot, free-list head, and mutation generation. Secondary headers carry their own entry counts and key descriptors. `i_cfrd` loads schema from `.CFG` and then replaces its mutable counters with durable key-zero metadata.

Deleted slots form a singly linked list inside `.DAT`. Byte 0 remains the `0xFF` delete marker and bytes 1-2 hold the next physical slot as a little-endian 16-bit value; `0xFFFF` ends the list. Delete writes the marker and link with the record in one operation, and insert reads the link from the deleted record it already loads. Fixed records must therefore be at least three bytes long.

In DXISAM:

- Records are fixed length and addressed by logical index.
- Up to four contiguous record fields (`keyoff[0..3]`, `keysz[0..3]`) are unique durable keys. Include key zero as a suffix when a secondary value may repeat.
- Deletes simply mark the first byte of the physical record with `0xFF`, preserving file layout and enabling reuse.
- Configuration (`g_cfg`) tracks logical (`nrecs`) versus physical (`maxrec`) counts so that applications can iterate all slots quickly and decide whether a record is active.

Exact lookup and delete are `O(log n)` index reads; delete then performs one entry write. Ordered traversal reads index entries sequentially and skips tombstones. Out-of-order insertion and key-changing update can still shift index entries, so those mutations remain `O(n)` in this flat-index release.

## Build

Compile `isamdb.c` as the only library source. The build amalgamates configuration, record I/O, optional journaling, verification, and durable indexes into one dcc translation unit. Do not compile `dxisam.c`, `dxfile.c`, or `dxpkey.c` separately; doing so can produce collisions between dcc-generated private symbols.

The old in-memory sparse index is excluded by default because it duplicates the durable `.IDX` index and requires application-maintained synchronization. Existing callers that still need `ixbld`, `ixlook`, `ixins`, or `ixdel` can opt in with `DXISAM_LEGACY_INDEX`; `isamdb.c` then includes `dxindex.c` and `isamdb.h` exposes its types and API.

M80 keeps only six significant external-symbol characters, including dcc's leading underscore. Public names therefore differ within their first five C characters (`pki_*`, `key_*`, and `cur_*`).

```text
dcc-input=doctor.c,isamdb.c
dcc-define=DXISAM_DISABLE_JOURNAL
dcc-stack-check=true
dcc-stack-bytes=768
```

Run `build-app.sh` to build both `DOCTOR.COM` and `DOCGEN.COM`. Each program is
linked separately with its own stripped DCC runtime.

For a legacy compatibility build, add:

```text
dcc-define=DXISAM_LEGACY_INDEX
```

The standard DOCTOR build enables dcc's runtime stack checker with a 768-byte stack and disables rollback journaling to leave room for four patient indexes. DXISAM keeps its 1 KB configuration buffer and 128-byte record scratch areas in shared static workspaces instead of automatic storage.

### Footprint

The multi-index DOCTOR build retains roughly 500 bytes of Link-80 workspace after linking. Its main space-saving choices are:

- Compile-time removal of journaling in this rebuildable demonstration.
- Shared table lookup and CP/M filename construction.
- Logical reads delegating to the physical read path.
- Index-based metadata loading without a second table search.
- Shared static configuration/record workspaces instead of large stack buffers.

## DXISAM Concepts

- **Database configuration (`g_cfg`)**: Holds database name, table list, record sizes, key fields, and counters. Applications populate it before calling `i_cfwr`/`i_cfrd`.
- **Tables**: Each table entry defines a name, record size, and key metadata. The serialized `disk` byte is retained for configuration compatibility but is not used by DCCRTL file I/O. Files are created on the current CP/M drive.
- **Records**: Fixed-length byte arrays (padding with zeros) that the caller constructs before passing to the library.
- **Logical vs. physical slots**: `nrecs` counts active rows; `maxrec` reflects the high-water mark of physical slots (including deleted entries). Traversing logical records uses `i_findlog`, while `i_rdphys` exposes raw slots for scans.
- **Lazy delete flag (`0xFF`)**: The first byte of a deleted record is set to `0xFF`, and bytes 1-2 hold its free-list link. `i_insrt` pops the embedded free-list head in constant time before growing the table.

### Return Codes

| Constant   | Value | Meaning                              |
|------------|-------|--------------------------------------|
| `I_OK`     | 0     | Success                              |
| `I_EOPEN`  | -1    | Failed to open or create a file      |
| `I_EWRIT`  | -3    | Write failed                         |
| `I_ENTBL`  | -4    | Table not found in configuration     |
| `I_ESIZE`  | -5    | Record size mismatch or too large    |
| `I_EREAD`  | -6    | Read failed                          |
| `I_ENREC`  | -7    | Invalid record number / deleted slot |
| `I_EUPDT`  | -8    | Update or delete rewrite failed      |
| `I_EDUP`   | -9    | Duplicate configured key             |
| `I_EINDEX` | -10   | Missing or invalid index             |
| `I_EVERIFY`| -11   | Table consistency check failed       |
| `I_ERECOV` | -12   | Journal validation or recovery failed|

## API Reference

The functions below operate on the global `g_cfg` instance. Callers must populate `g_cfg` before creating tables or records.

| Function & Signature | Description |
| -------------------- | ----------- |
| `int i_cfwr(char *cfgname);` | Write the current `g_cfg` structure to a configuration file (e.g. `PATIENTS.CFG`). Creates or truncates the file with dcc runtime `open`/`write` calls. |
| `int i_cfrd(char *cfgname);` | Load `g_cfg` from a configuration file written by `i_cfwr`. Updates table metadata and record counters. |
| `int i_mktbl(char *table);` | Create an empty `TABLE.DAT` file on the current drive for the named table defined in `g_cfg`. Validates the table entry first. |
| `int i_insrt(char *table, char *record, int size);` | Insert a record. Verifies the size, pops a deleted slot from the free list when available, and appends otherwise. Updates durable metadata automatically. |
| `int i_rdrec(char *table, char *dest, int logicalIndex);` | Read an active record by logical index (0-based). Uses `i_findlog` internally to skip deleted slots. Returns `I_ENREC` if the index is invalid. |
| `int i_rdphys(char *table, char *dest, int physicalIndex);` | Read a physical slot without logical filtering. Returns `I_ENREC` when the slot is marked deleted; useful for full scans and diagnostics. |
| `int i_uprec(char *table, char *record, int size, int logicalIndex);` | Update a logical record in place. Writes a full record buffer back to the corresponding physical slot. |
| `int i_delrec(char *table, int logicalIndex);` | Lazily delete a record by logical index. Sets the first byte to `0xFF` and decrements `nrecs`, leaving `maxrec` unchanged for reuse. |
| `int key_find(const char *table, const char *key, char *record);` | Find an exact primary key. Returns its physical slot, or a negative error code. `record` may be null. |
| `int idx_find(const char *table, int keyIndex, const char *key, char *record);` | Find an exact value in any configured key. |
| `int cur_first(const char *table, struct i_cursor *cursor, char *record);` | Read the first record in primary-key order and initialize a cursor. Returns its nonnegative physical slot. |
| `int idx_open(const char *table, int keyIndex, const char *prefix, int prefixLength, struct i_cursor *cursor, char *record);` | Start at the first entry matching a key prefix. Subsequent `cur_next` calls stop at the end of that prefix range. |
| `int cur_next(const char *table, struct i_cursor *cursor, char *record);` | Read the next record in primary-key order. Returns its nonnegative physical slot, or `I_ENREC` at the end. |
| `int pki_rebuild(const char *table);` | Recreate and compact every configured index by scanning active physical records. |
| `int table_verify(const char *table, struct i_verify *report);` | Validate every index against `.DAT`, plus metadata, active/deleted counts, and the free chain. |

### Helpers and Globals

- `struct i_db g_cfg;` contains the loaded schema and cached counters. Mutable counters are committed in `.IDX`; applications do not need to rewrite `.CFG` after each mutation.
- `i_findlog` is a compatibility helper that maps logical indices to physical slots. Most applications rely on `i_rdrec` instead of calling it directly.
- `i_mktbl`, `i_insrt`, `i_wrphys`, `i_uprec`, `i_delphys`, and `i_delrec` create or maintain every configured index automatically.
- A cursor is invalid after a table mutation. Restart traversal with `cur_first`.
- `pki_rebuild` reconstructs `.IDX` and all `.IXn` files, rewrites embedded free links, and removes tombstones. Opening a table automatically rebuilds all indexes when a secondary file is missing or incompatible.

### Consistency Model

By default, each insert, update, or delete first writes and flushes a checksummed `DXJ1` undo record containing the old physical record and metadata. The library then flushes `.DAT`, all index changes, and key-zero metadata before committing and clearing the journal. Recovery restores `.DAT` and rebuilds every configured index.

Defining `DXISAM_DISABLE_JOURNAL` removes journal state and code. This compact profile maintains every index during normal mutations and verifies every index against `.DAT`, but it cannot roll back an interrupted write. On reopen, a missing or incompatible secondary index triggers reconstruction of all indexes from `.DAT`; callers can also invoke `pki_rebuild`. The surgery demonstration uses this profile because its complete database is reproducible with `DOCGEN` and CP/M memory is constrained.

DXISAM caches `.DAT` plus one index descriptor for one active table. Switching tables or index numbers, creating files, recovery, and rebuilds invalidate the affected descriptors.

The embedded free-list format uses the `DXI3` index header. Existing `DXI2` tables require an export/recreate/import migration because their deleted records do not contain links. Creating a table removes stale legacy `C:TABLE.*`-style files produced by older DXISAM builds, including `.MAP`.

DCCRTL's `open` function accepts an 8.3 filename on the current drive but does not parse a leading `A:` through `P:` drive selector. Change to the desired drive before launching an ISAM application. Passing `C:PATIENTS.DAT` to DCCRTL creates a malformed literal basename such as `C:PATIEN.DAT`; DXISAM therefore never puts a drive prefix in an I/O filename.

In journaled builds, the test-only `i_test_crash_after` hook injects failures at transaction boundaries. In compact builds it is a no-op. `pktest.c` always covers multi-key insert, update, delete, prefix ranges, verification, and rebuild; journaled builds additionally cover injected crash recovery.

## Usage Examples

### Multi-table surgery database (`docgen.c` and `doctor.c`)

```c
g_cfg.ntbls = 3;
configure_table(0, "DOCTORS", 64, 3); /* Doctor ID: DDD */
configure_table(1, "PATIENTS", 126, 5); /* Key 0: patient ID */
g_cfg.tbls[1].nkeys = 4;
g_cfg.tbls[1].keyoff[1] = 91;  /* surname prefix + patient ID */
g_cfg.tbls[1].keysz[1] = 15;
g_cfg.tbls[1].keyoff[2] = 106; /* first-name prefix + patient ID */
g_cfg.tbls[1].keysz[2] = 12;
g_cfg.tbls[1].keyoff[3] = 118; /* doctor ID + patient ID */
g_cfg.tbls[1].keysz[3] = 8;
configure_table(2, "HISTORY", 112, 8); /* Patient + sequence: PPPPPSSS */

i_cfwr("SURGERY.CFG");
i_mktbl("DOCTORS");
i_mktbl("PATIENTS");
i_mktbl("HISTORY");
```

Run `DOCGEN` to recreate the database with 8 doctors, 240 patients, and 720 history entries. This is required once after upgrading from the old 96-byte patient schema. `DOCGEN.COM` owns table creation and synthetic data; `DOCTOR.COM` contains only the production CRUD interface and loads the persisted database without regenerating it. Both sources include `SURGERY.H` so record sizes and field offsets remain synchronized. Patient-to-doctor joins and patient-history records use exact key-zero lookups. Each patient stores its history count, allowing complete composite history keys to be constructed directly.

After loading the data, `DOCTOR.COM` opens an 80-column by 30-row VT100 interface. Run `DOCTOR /V` to verify every table, index, relationship, and roster before opening the interface. The doctor directory leads to a paged patient roster and then to clinical history. In a patient roster, `A` adds a patient and `E` edits patient details. Patient updates automatically replace changed surname and first-name index entries. Patient-name search runs case-insensitive prefix ranges over both indexes, merges duplicate-free surname and first-name matches, and presents a paged list from which Enter opens the selected patient. In patient history, `A` appends a note and `E` edits the selected note with insertion, Cursor Left/Right, Backspace, and forward Delete. Dates are entered as guided `DD/MM/YYYY` digits with calendar validation, then stored in the existing sortable `YYYYMMDD` record format.

Press `D` in the doctor directory to open ISAM activity diagnostics. Session counters distinguish exact primary-key lookups and binary-index comparisons from ordered index-cursor traversal and true `.DAT` logical table scans. The view also reports physical record reads. Counters reset before the interface opens so they describe interactive activity; press `R` in the diagnostics view to reset them again.

Doctor rosters use a bounded prefix range on the `doctor ID + patient ID` secondary index. A 30-patient roster therefore visits 30 index rows rather than scanning all patients or following stored links. VT100 output is unbuffered so Link-80 can fit the merged patient-search UI in the CP/M transient program area.

### Inspecting physical slots and slot reuse (`isamtst8.c`)

```c
/* Scan every physical slot and sample deleted ones */
for (i = 0; i < g_cfg.tbls[0].maxrec; i++) {
    if (i_rdphys("NAMES", rbuf, i) == I_ENREC) {
        sample_slots[sample_count++] = i;
        continue;
    }
    printf("Rec %d: ID=%.4s\r\n", count + 1, rbuf);
}

/* Insert new records and confirm reuse */
for (i = 0; i < 3; i++) {
    /* build rec[...] */
    i_insrt("NAMES", rec, 32);
}

printf("After inserts: nrecs=%d maxrec=%d\r\n",
    g_cfg.tbls[0].nrecs, g_cfg.tbls[0].maxrec);

/* Show that deleted slots now contain the new rows */
for (i = 0; i < sample_count; i++) {
    i_rdphys("NAMES", rbuf, sample_slots[i]);
    printf("Slot %d -> ID=%.4s\r\n", sample_slots[i], rbuf);
}
```

The `isamtst8` utility demonstrates the lazy-delete strategy end to end: records are deleted, new inserts reuse the freed physical slots, and `maxrec` remains unchanged. Reloading the configuration via `i_cfrd` proves that the persisted counters survive restarts.

## Validation

- `pktest.c` covers primary and secondary exact lookup, prefix ranges, multi-key insert/update/delete, deleted-slot reuse, verification, corruption detection, and all-index rebuild. Journaled builds also cover injected transaction failures.
- `isamtst.c` validates logical reads/updates/deletes, physical scans, durable counts, and slot reuse with 1,000 records.
- `docgen.c` creates the three-table surgery database and generates 968 related records.
- `doctor.c` provides indexed CRUD, joins, ordered traversal, and startup verification without carrying generation code in the production binary.

Refer to `pktest.c`, `docgen.c`, and `doctor.c` for complete CP/M examples.
