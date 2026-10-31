# Banked ISAM Library

The Banked ISAM Library is a generic fixed-record ISAM library for dcc / CP/M 2.2. The engine,
database configuration, file descriptors, indexes and caches live in **bank 1**.
DOCTOR and DOCGEN are **bank-0 clients**. The only client surface is the native
`db_*` API in [isam.h](isam.h); there is no configuration mirror, automatic
schema synchronization or low-level index-edit API in the client.

## Building and running

Set `DCC_DIR` to the matching DCC toolchain and run `dccmake` here. The
[composite build](dccmake.txt) builds these together:

| Output | Bank | Responsibility |
| --- | --- | --- |
| `lib/build/ISAM.LIB` | 1 | Generic database engine and all database I/O |
| `doctor/build/DOCTOR.COM` | 0 | Surgery UI and application records |
| `docgen/build/DOCGEN.COM` | 0 | Application-defined schema and synthetic data |

This follows [LIBDEMO](../../disk-b-apps/LIBDEMO/README.md).
DCC generates the contracts, host wrappers and bank-1 adapters from
[ISAMLIB.C](lib/ISAMLIB.C); do not edit generated files.
Clients link [isclient.c](isclient.c), generated bindings and the shared
[BANKSDK](../../disk-b-apps/BANKSDK/README.md), **not the engine**.
Rebuild and deploy the library and clients together: the old banked ABI is
not supported. `DXC4` configurations and `DXI4` indexes replace the old formats;
regenerate old databases with DOCGEN. There is no legacy reader or migration.

Run `pwsh ./build-app.ps1` to build and install all three artifacts on D:
([disks/blank.dsk](../../../disks/blank.dsk)). `-BuildOnly` collects the files
without changing a disk. The top-level [build script](../../build-all.ps1)
discovers the same three-child build.

```text
D>DOCGEN
D>DOCTOR /V
```

DOCGEN replaces existing data. The generated database (about 220 KiB of
history data plus about 65 KiB of patients, doctors and indexes) fits the 502K D:
disk with the programs and library, leaving room for the sort temporaries; delete
partial database files and rerun DOCGEN after resolving an `rc=-3` write failure
(for example, a full disk).
Use `-BuildOnly` when preparing the updated programs without deploying them.

Use the bank-capable **Z80ROMlessSBC** emulator, not ntvcm or the old single-bank
launcher. Typed libraries require BIOS ABI 2.6, allocator revision 6 and the
version-4 library loader. Source names must be CP/M 8.3 compatible.

## Native client API

Include [isam.h](isam.h), not the private engine header
[isamdb.h](isamdb.h). Each process owns one library/database session.

```c
int patients;
char record[91];

if (db_open("SURGERY.CFG") != I_OK)
    return 1;
patients = db_table("PATIENTS");
if (patients < 0)
    return 1;
memcpy(record, "00001", 5);
if (db_get(patients, 0, record, sizeof(record)) < 0)
    return 1;
return db_close() == I_OK ? 0 : 1;
```

`db_open` loads `ISAM.LIB`. Its `onload` initializes generic bank-1 state;
the subsequent open request supplies the database structure filename. Bank 1
reads the schema, restores durable counters and opens/validates the described
data/index files. Missing databases fail without creating or truncating them.
There are no surgery table names or filenames in the library.

Resolve table names once with `db_table`; operations thereafter take compact
integer handles. Handles are zero-based schema positions and valid only for
the current database session. `db_info` explicitly queries a table definition
and current counters; this is a snapshot, not mutable client-side engine state.
`onunload` closes descriptors, frees the private cache and reports close errors.
Transport failures are reported explicitly and become `I_EBANK`.

### Creation

Pass an application-defined `struct db_schema` to
`db_new(configuration, &schema)`. It loads an empty library session, validates
the schema, creates all data/index files and writes the configuration in bank 1.
Creation is destructive for those table files and is not an atomic multi-file
transaction. Invalid schemas, including duplicate table names, are rejected
before table creation. Configuration is written only after table creation succeeds.

```c
struct db_schema schema;
memset(&schema, 0, sizeof(schema));
strcpy(schema.name, "EXAMPLE");
schema.tables = 1;
strcpy(schema.table[0].name, "ITEMS");
schema.table[0].size = 32;
schema.table[0].keys = 1;
schema.table[0].widths[0] = 4;
if (db_new("EXAMPLE.CFG", &schema) != I_OK)
    return 1;
/* Table 0 is ITEMS. Build records and call db_add/db_batch. */
```

The creation schema is 126 bytes and contains no live counters. It supports
three tables, four keys per table, 3-128-byte records and 1-15-byte keys.
Each key has a first component and an optional second component:

- `widths[k]` is the total key width.
- `extra[k]` is the second component's width; zero means a contiguous key.
- `offsets[k] & 127` locates the first component, whose width is
  `widths[k] - extra[k]` and must be positive.
- `tails[k]` locates the second component.
- Set `I_KEY_UPPER` in `offsets[k]` for ASCII uppercase normalization of the
  first component only. The second component is copied verbatim.

The engine derives keys during insertion, update, deletion, verification and
rebuild. Each key is unique; append a primary-key component to secondary
values that may repeat, without storing another copy in the record.
Lookup values and prefixes must already have the configured normalized
layout. Applications cannot change schema during a session.

### Operations

| Call | Result |
| --- | --- |
| `db_open(configuration)` | Open an existing database; `I_OK` on success |
| `db_new(configuration, &schema)` | Create a database and leave its session open |
| `db_close()` | Unload before releasing client-owned shared storage |
| `db_table(name)` | Table handle, or negative error |
| `db_info(table, &info)` | Definition, active records, physical slots and primary index entries |
| `db_get(table, key, record, size)` | Consume the search key at the **start** of the buffer, replace it with the record, return physical slot |
| `db_probe(table, key, value, keyBytes)` | Return physical slot without reading the data record |
| `db_read(table, slot, record, size)` | Read an active physical slot; return `I_OK` |
| `db_add(table, record, size)` | Insert and maintain all indexes |
| `db_put(table, slot, record, size)` | Update a physical slot and all changed keys |
| `db_del(table, slot)` | Delete a physical slot and maintain all indexes |
| `db_verify(table, &report)` | Verify indexes, data, counts and free chain |
| `db_rebuild(table)` | Rebuild/compact every index from active records |
| `db_build(table)` | Start exclusive bulk creation of an empty table |
| `db_append(table, records, bytes, &completed)` | Append a packed batch during creation |
| `db_finish(table)` | Sort/build all indexes and publish the completed table |
| `db_stats(&stats)` / `db_reset()` | Query/reset activity counters; failures are explicit/fatal |

Read/write sizes must match the configured record size. GET's search key is
supplied at buffer offset zero even for a secondary key; the returned record
has its normal layout. This permits reading directly into the caller's buffer
without an intermediate response copy. PROBE takes only the key bytes.

For individual cursor rows, call
`db_first(table, key, prefix, length, &scan, record, size)`, then
`db_walk(&scan, record, size)`. Both return physical slots, or `I_ENREC` at end.
A null record with size zero performs key-only traversal. Cursor state has
the same layout in both banks: no field-by-field translation. Restart a cursor
after a table mutation.

### Bulk transfers

`db_batch(table, records, bytes, &completed)` inserts **one packed batch of
at most 4096 bytes**. Bytes must be a whole number of records. The library
returns the number of committed records, including a committed prefix when an
error stops the batch. It is not an all-or-nothing transaction. Zero bytes
succeed without allocating shared storage. Callers explicitly queue/split
larger workloads rather than relying on a schema-mirroring adapter.

`db_scan(table, key, prefix, length, &scan, rows, capacity, mode)` starts a
bulk scan; `db_more(&scan, rows, capacity)` continues it. Capacity is in bytes,
capped at 4096. Results include `scan.count`, `scan.width` and `scan.bytes`.
Process the returned count even on the final `I_ENREC` response; a nonempty
last page is normal. `I_OK` means the current page filled.
`db_project(table, key, prefix, length, &scan, rows, capacity, view)` starts
the same traversal with projected record fields; `db_more` retains its view.
The counted scan contract is 46 bytes, including the view.

`db_page(&scan, position, rows, capacity)` rereads or jumps within an
initialized scan. It seeks directly to the inclusive, zero-based **index-entry
position**, then fills one packed batch. It does not walk preceding pages,
rescan a table, enumerate all slots, or do a key lookup per row. It preserves
the scan's key, prefix, mode and projection; `db_more` can continue the result.
The operation reuses the 46-byte control frame and 4 KiB lease.

Positions are not live-row offsets: deleted index entries are skipped while
filling the batch. For sequential page bookmarks, save
`scan.cursor.position + 1` after a full page, then use `db_page` at that position
to revisit it later. Save each page's starting position for backward navigation.
Unfiltered compact indexes start at zero; prefix scans start with the existing
binary-search-based `db_scan`/`db_project`. A full page returns `I_OK` even if it
ends exactly at EOF; a short/empty tail returns `I_ENREC`. Process `scan.count`
in either case. Invalid positions return `I_EINDEX`.
As with existing cursors, restart the scan and discard bookmarks after mutations
to that table, index rebuilds, or closing/reopening the database. Bookmarks are
not persistent IDs or mutation-stable snapshots.

| Mode | Row contents |
| --- | --- |
| `DB_RECORDS` | Full record |
| `DB_KEYS` | Configured key followed by a little-endian 16-bit physical slot |
| `DB_SLOTS` | Little-endian 16-bit physical slot only |

Key/slot projections skip tombstones without reading `.DAT`; record-field
projections read each source record. Individual and bulk
traversal share the embedded cursor state and can be mixed.
Within a bulk call, the index decoder positions the descriptor once and streams
successive encoded entries, including tombstones, without recalculating offsets
or copying each key through another buffer. Each subsequent API call positions
from its own cursor again, so interleaved scans and direct page jumps remain safe.
The same decoder handles CP/M sector and 64 KiB boundaries.

`db_fetch(table, slots, count, view, rows, capacity)` gathers an array of
physical slots in exactly the requested order, including duplicates. Pass a
null view for full records. Slots are native 16-bit integers; this handles
noncontiguous search results without one RPC per record.

`db_pick(table, key, values, count, value_size, view, rows, capacity)` does
the same using packed configured-key values rather than slots. `value_size`
must match that key's configured width. Use known slots when available to
avoid searching indexes again.
Ascending batches probe the next index entry after each match; repeated keys
reuse the matching position. A probe miss falls back to binary search, and
descending keys use binary search directly. The hint is private to that call:
sparse keys add at most one probe per lookup, never an unbounded forward scan.
Input order, duplicate results and error behavior are unchanged; callers do not
need to sort keys or use a new API or disk format.

Both use the same `db_pick` implementation and 21-byte counted control
contract. `struct db_view` specifies up to six byte ranges: `fields`,
`offsets[DB_FIELDS]` and `sizes[DB_FIELDS]`. Ranges are concatenated in
declaration order, including reordered or repeated ranges. The library
validates and prepares the copy ranges once per request. Combine adjacent
ranges in the caller when possible. Zero fields means full records. Every range
must be nonempty and within the record, and packed rows cannot exceed 128
bytes. These are generic byte layouts, not schema-specific library structs.

Count must be positive and the entire result must fit capacity. The shared
lease holds selectors at its tail and packed rows at its start, so
`count * (selector_width + row_width)` must not exceed 4096. For example,
full 128-byte records with two-byte slot selectors allow 31 rows per call.
The reply gives the exact output byte count and row width; it does not
silently truncate or return a partial prefix. On a database/validation error,
the caller's output is unchanged. Input selectors and output may alias in
bank 0 because selectors are transferred first. Bank 1 completes every read
before one `bread` transfers the result.

The client lazily allocates one reusable 4 KiB `bmalloc` lease. Bank 1 accesses
the registered storage directly. Payloads use one `bwrite`/`bread` per batch,
not per record or a series of 256-byte calls. Control frames remain small and
counted; bulk payloads do not travel inside RPC frames. The client validates
completion sizes. Writes to the lease first call `bank_library_check()`, so a
pool reset cannot corrupt or free a subsequent owner's allocation. Reads skip
that query: the library call that follows validates the pool epoch itself, and a
stale epoch clears the lease and returns `I_EBANK`.

The shared 5632-byte pool holds the loader workspace (512 bytes), the bulk
lease (4096) and 1024 free bytes. Scalar-only clients do not allocate the lease.
Bank 1 never calls the bank-0 allocator gateways. The five exports are
`onload`, `onunload`, `iscall`, `ispost` and `iswalk`; the first two are lifecycle
hooks. IN annotations avoid copying insertion input back into the client.
The BIOS still round-trips complete call frames, so reducing frame lengths
and batching matter in addition to direction annotations.

### Sorted bulk creation and rebuild

For new tables, call `db_build`, queue whole-record batches of up to 4096
bytes with `db_append`, then call `db_finish`. Only one table can be under
construction. Reads, ordinary mutations and cursor traversal are unavailable
until completion; ordinary `db_batch` remains the incremental alternative.

The builder appends data sequentially, sorts bounded 2 KiB runs and merges
them into sequential index output. Rebuild uses the same path and reconstructs
the free-slot chain without moving physical records. Bank 1 borrows its
existing private 4 KiB cache during sorting, rather than allocating another
buffer. Merge offsets use 32-bit arithmetic.

A durable `TABLE.BLD` marker prevents an interrupted or failed build from
opening as a valid table. There is no rollback or resumable build: fix the
cause and explicitly regenerate the database. Duplicate final keys fail with
`I_EDUP`; an unfinished table returns `I_EBUILD`, including after reload.
During rebuild, the target index is truncated after the marker is written.
Temporary `.RN0`/`.RN1` files require room for two index-sized runs, plus CP/M
allocation/directory overhead and the other database files; the unused
run is removed before final index output, avoiding three concurrent copies.
Failures attempt temporary-file cleanup but retain the blocking marker.

## Storage, correctness and durability

Files use the current CP/M drive: change drive before launching the program.
DCCRTL does not parse a drive prefix in file I/O names.

- `TABLE.DAT` contains stable fixed-size physical record slots.
- `DATABASE.CFG` contains the `DXC4` magic and native pointer-free schema.
- `TABLE.IDX` stores primary-key entries; `.IX1` through `.IX3` store secondary
  keys. Entries contain the key and a physical slot.
- The checksummed 24-byte `DXI4` primary header is authoritative for active counts,
  physical high-water mark, index entries, free-list head and generation.
  Configuration counters are replaced from durable metadata on open.
- Deleted records start with `0xFF`; bytes 1-2 link the next free slot, with
  `0xFFFF` as the only negative sentinel. Application records cannot start
  with `0xFF`. Inserts reuse slots in constant time.
- Tombstoned index entries retain sorted keys. Primary and secondary
  tombstone counts may differ; that alone does not trigger a rebuild.
- Mutations preflight all changed keys before writes, reuse saved search
  positions and rotate only the changed index interval. Duplicate and capacity
  rejection cannot corrupt the free list or partially modify that record.
- Signed slot and index counts are bounded by 32767. Lookup/projection paths
  reject dangling physical slots even without reading the record.

Experimental journaling has been removed, including recovery, crash-injection
controls and alternate build profiles. No `.JRN` files are created or used.
Mutations still flush data before index changes and publish durable counters
last, but they are not crash-atomic: interrupted writes cannot be rolled back.
Missing/incompatible secondary indexes can be rebuilt on open; verification
and explicit index rebuilding remain available.

Bank 1 retains six reference-counted LRU descriptors, leaving two of DCC's
eight descriptors for configuration and other file work. A two-way read-through
cache uses 4 KiB. The private engine uses numeric handles and pins descriptors
once per read batch. Binary search returns the matching slot without rereading
its index entry. Validated index headers are cached per descriptor.
Writes invalidate overlapping pages; short/failed writes discard the file's
cached pages. Stable table/index identities preserve unaffected pages across
real descriptor closes. These closes remain mandatory at normal mutation
boundaries: DCC's `fsync` alone does not publish CP/M directory metadata.
Create, schema load, rebuild and unload discard cached state.
Verification reopens descriptors to detect external corruption. Other reads
assume exclusive database ownership. Statistics reset does not clear caches.

Error codes are defined in [isdefs.h](isdefs.h): `I_EOPEN=-1`,
`I_EWRIT=-3`, `I_ENTBL=-4`, `I_ESIZE=-5`, `I_EREAD=-6`, `I_ENREC=-7`,
`I_EUPDT=-8`, `I_EDUP=-9`, `I_EINDEX=-10`, `I_EVERIFY=-11`,
`I_EBANK=-13`, `I_ERECORD=-14`, `I_EFULL=-15`, `I_EBUILD=-16`.
`I_ENREC` also denotes a deleted/missing slot or end of a range.

## Surgery applications

[docgen.c](docgen.c) defines the surgery schema, creates 6 doctors, 300 patients
and 1998 history entries, and queues sequential build appends in a 4 KiB buffer.
The doctors have 40, 44, 48, 52, 56 and 60 patients respectively. Patient IDs and
the stored first-patient/count fields follow those variable roster boundaries.
Histories are intentionally skewed to fit the disk: 96 patients each have 5, 6
and 7 records, six have 15 records, and six have 30 records. Every 25th patient
has 15 visits, except every 50th has 30. The generator refuses to replace data
if its computed history total exceeds the 2040-record cap.
The count stored in every patient record matches its generated history rows.
The first visit is registration; subsequent visits alternate consultation and
follow-up. Monthly dates roll into subsequent years, reaching June 2025 for
30 visits rather than producing invalid month numbers.
All six doctors have names and a specialty.
[doctor.c](doctor.c) implements the 80-column by 30-row VT100 CRUD interface.
Both use [surgery.h](surgery.h); the library does not include that schema.
Patient records are 91 bytes instead of 126: the engine derives normalized
name/ID and doctor/ID keys, removing 35 duplicated bytes per patient
(10500 raw bytes for 300 patients).

DOCTOR uses projected indexes for patient search, roster joins and finding
the next patient ID. The UI then batch-fetches up to 20 doctors (1280 bytes),
up to 20 projected patient rows (800 bytes instead of 1820), or three full
history entries (336 bytes). Each packed patient row contains only its ID,
first/last names, age, gender and visit count: 40 bytes. Roster/search
projections retain physical slots, avoiding
a second key lookup per displayed patient. One reusable, heap-allocated 2560-byte
bank-0 page buffer holds the current screen; it does not enlarge the stack.
History slot IDs occupy the unused portion of that same buffer.
Three history records plus 999 slot IDs need 2334 bytes; remaining capacity
allows the loader to detect excess rows. This saves 1536 bytes of bank-0 RAM.
The doctor directory is page oriented (20 rows, `DOCTOR_PAGE_SIZE`) and is not
bound to the current six doctors. Its initial scan reads only the first page;
`db_page` then seeks directly to each requested page without a full-directory
slot array. Page boundaries are aligned, so moving through a page only changes
local highlighting, rather than repeatedly fetching overlapping rows. Returning
from a patient screen reloads only the visible page; closing the statistics
screen uses the cached page. Search can jump straight to an off-screen doctor.
The surgery schema retains contiguous doctor IDs 001 through N (up to 999);
page loading and `/V` validate this, independently of DOCGEN's configured count.
The test fixture uses a 5-row page to exercise forward/backward navigation.

Rendering and highlight changes use local records only. Page changes refill
the batch; edits, additions and returns from nested screens refresh the cache.
Patient rosters/search results and histories now use aligned 20-row and
three-entry pages too: crossing a boundary displays the next/previous page,
rather than scrolling an overlapping window and reloading it on every arrow.
Up/Down within the new page performs no database calls.
Doctor-page navigation repaints only the directory rows, leaving the header
and footer intact. Screen transitions still redraw the complete screen.
Patient/doctor rows erase trailing text after painting instead of blanking the
whole line first; page painters no longer clear populated rows twice.
History highlighting repaints only the date/type heading, not unchanged notes.
Short pages explicitly clear unused rows to prevent stale entries. Long words
in notes wrap across lines instead of losing characters beyond the line width.
Each navigation update flushes the completed output, retaining responsive
keyboard handling without adding a screen-sized buffer.
The library remains schema-independent.
DOCTOR also batches doctor-name search and relationship verification.
Verification validates the contiguous doctor-ID range in batches, scans 11-byte patient relationship
rows and accumulates history keys in batches of 64, returning only the doctor-ID
field needed to check each link. Missing keys or mismatched doctor IDs fail explicitly.
Individual edits still use exact record lookups. `DOCTOR /V` verifies all
tables and relationships before startup. The doctor directory leads to patient
rosters and clinical history. `A` adds a patient/note, `E` edits details/notes,
`P` searches patients, and `D` in the directory shows/reset activity counters.

DOCTOR has a 2048-byte checked stack; DOCGEN retains 768 bytes. Regression tests
repeat roster -> search -> history -> search -> roster, which overflowed the
earlier 768-byte DOCTOR stack. The library reserves 1024 bytes with stack
checking disabled because DCC's transient panic startup is not library-safe.

## Size and performance

The native redesign removes client configuration/counter synchronization,
name resolution on every operation, generic dispatchers, intermediate
record buffers and cursor translation.

| Retained DOCTOR component | Previous bytes | Native bytes |
| --- | ---: | ---: |
| Client implementation | 6679 | 4505 |
| Generated bindings | 4739 | 1524 |
| Shared bank SDK | 5761 | 5766 |

The native builds initially reduced DOCTOR from 40192 to 34176 bytes. Adding
generic slot/key gathering, projections, actual screen-page caches and batched
verification and the paged doctor directory made it 38016 bytes. Terminal redraw
improvements bring it to 38272 bytes. A later client cleanup (below) brought it
to 37504 bytes, still with the same 2560-byte page workspace and 2048-byte
checked stack.
DOCGEN is 19328 bytes including variable rosters, skewed histories, the cap and
the 94 first-name / 87 last-name tables (names are chosen by a deterministic
16-bit hash, so reruns generate identical data).
The library is 43136 bytes. Client + generated code is 5519 bytes versus the old
11418 (52% smaller); total bank plumbing is 11308 versus 17179 (34% smaller).
Regression budgets: client + generated code at 5632 bytes, total bank plumbing at
11776, DOCTOR at 37888, DOCGEN at 19456 and `ISAM.LIB` at 43264.
The preceding optimization pass saved 128 DOCTOR binary
bytes and 1536 workspace bytes; DOCGEN's binary size was unchanged. The generic
builder/composite-key support adds 2432 library bytes, offset on disk by the
5250 raw patient-data bytes removed. These are retained binary sizes,
not source-line counts; the 4 KiB page cache is allocated at runtime.
The bank-1 heap has 1248 bytes left after allocating that cache. Tests reserve
at least 512 bytes of headroom plus the unchanged 1024-byte stack.

### Library cleanup pass

The page cache (`isacache.c`) was the dominant library cost: a PC-sampling
profile of a DOCTOR paging workload put `page_read` at 24% of bank-1 time, with
`page_seek` and `memcpy` adding another 13%. It tracked positions as 32-bit
`long` values, which dcc compiles to long inline arithmetic on every hit. Positions
are now an unsigned sector plus a byte offset, so a cache hit uses only 16-bit
arithmetic; only a miss builds a 32-bit offset for `lseek`. The set mapping,
replacement policy and invalidate-on-write behaviour are unchanged, so hit/miss
counts are identical in every test. The engine seek is origin-less (every caller
already seeks absolutely) and offsets of 8 MiB or more are rejected. Other changes:

- six uncalled source functions (`key_find`, `key_add`, `key_remove`,
  `key_update`, `cur_first`, `rebuild_free_link`) and their prototypes removed;
- `stats_add` is a saturating-increment macro (all callers add 1);
- `DB_INFO` copied the table name from the table *index* used as a pointer; it
  now copies `g_cfg.tbls[table].name`, and BANKTST checks the name and size;
- the redundant `name = table` aliases in `iscall`/`ispost` are gone;
- `compare_keys` is `static inline` (a one-line `memcmp` wrapper), which saves
  0-1.5% CPU at no size cost. Inlining the 16-bit little-endian codecs and the
  `table_valid`/`key_valid` checks was measured and rejected: they have many
  call sites and grew `ISAM.LIB` by 768-1024 bytes for under 1% speed. Inlining
  the client's `transport_result` added 256 DOCTOR / 128 DOCGEN bytes for 0.1%.

`ISAM.LIB` shrank 896 bytes (44032 -> 43136); DOCTOR and DOCGEN are unchanged.
Raising the descriptor cache from 6 to 8 entries was measured and had no
effect, so it stays at 6. Measured on the emulator (T-states, before -> after;
selections, reads and writes are unchanged):

| Workload | Before | After |
| --- | ---: | ---: |
| BPERF 3 (key lookups) | 1,875,183 | 1,739,123 |
| BPERF 5 | 3,483,843 | 3,187,235 |
| BPERF 8 (scalar rows) | 23,892,259 | 22,980,681 |
| BPERF 10 (bulk rows) | 9,433,024 | 8,420,650 |
| BPERF 11 | 3,105,618 | 2,617,690 |
| BPERF 12 | 648,556 | 573,683 |
| Index page (first) | 226,032 | 209,149 |
| Index work 59 | 1,686,139 | 1,495,018 |
| Index work 60 | 606,319 | 520,485 |
| Single-row inserts | 42,559,068 | 41,483,939 |
| Index build, scalar | 725,942,524 | 684,287,110 |
| Relations, scalar | 194.5M | 180.2M |
| Relations, batch | 53.8M | 49.7M |
| VT100 page (66) | 804,153 | 797,725 |

CPU falls 0-12% depending on how read-heavy the phase is (the biggest gains are
in index-page and projected-key phases); the CP/M file system itself is now the
largest remaining cost.

### Client cleanup pass

This pass removed the unused standalone engine build (`ISAM_BANK_LIBRARY`
conditionals), the logical-record API (`i_rdrec`, `i_uprec`, `i_delrec`,
`i_findlog`) with its scan counters, the `db_lookup` alias (use `db_pick`), the
direct-link test programs and the old operation-number gaps. In the client, every
bulk fetch dropped a library-control epoch query and a redundant size check
(`bread`/`bwrite` already check sizes and each library call validates the pool
epoch); lease writes use the new `bank_library_check()`. DOCTOR shrank 640 bytes
and DOCGEN 384 bytes. `ISAM.LIB` is unchanged because it never contained the
standalone code. Measured on the emulator (T-states, before -> after):

| Workload | Before | After |
| --- | ---: | ---: |
| BPERF 1 (scan) | 718,605 | 717,725 |
| BPERF 5 | 3,483,043 | 3,483,843 |
| BPERF 10 (bulk) | 9,476,810 | 9,433,024 |
| BPERF 11 | 3,112,639 | 3,105,618 |
| BPERF 12 | 655,577 | 648,556 |
| Index page (first) | 233,053 | 226,032 |
| Relations, scalar | 194.8M | 194.5M |

Bank selections fell for BPERF 10, 11 and 12 (3032 -> 2960, 260 -> 248,
88 -> 76), index pages (130 -> 118) and scalar relations (52,324 -> 51,736);
other phases are unchanged. A few workloads are within 0.2% either way (for
example index work 58 and VT100 66 are about 0.2% slower). The per-call guard is
a macro so its early returns leave the caller directly; a separate call frame
cost more than the check it wrapped.

Indexed-page regression: five 128-byte rows at positions 0 and 35 each require
exactly five index rows, five data-record reads and zero preceding-row scans,
cursor starts or key comparisons. With both pages warm, the measured costs are
239771 and 244379 T-states, with 65 bank entries each (including control-frame
and payload transfers, not 65 database calls). Page depth adds no scan work.

Streamed-index and ordered-lookup measurements against the immediately preceding
implementation:

| Workload | Before T-states | After T-states | CPU change |
| --- | ---: | ---: | ---: |
| 20 ascending full-record key lookups | 1878886 | 865155 | 54% less |
| Same 20 keys reversed | 1878893 | 1895513 | 0.9% more |
| 40 secondary entries with 15-byte keys | 875099 | 644466 | 26% less |
| Batched DOCTOR relationship verification | 148189380 | 67184326 | 55% less |

Ascending lookups need 25 index comparisons instead of 94; reverse-order batches
still need 94. Both batches retain 378 bank selections and two sector reads.
The warm secondary scan retains 136 selections and zero sector reads.
All these read workloads perform zero writes. Tests require at least 40% and
20% CPU reductions for ordered lookups and streaming, respectively, and bound
reverse-order overhead to 5%. DOCTOR benefits through its existing batch APIs;
no application-specific fast path or additional client workspace is needed.

Earlier Z80ROMlessSBC native-boundary optimization measurements on the smaller
8-doctor/240-patient/720-history regression fixture, versus the final old
banked boundary (not measurements of the expanded production dataset):

| Workload | Previous T-states | Native T-states | Previous / native bank selections |
| --- | ---: | ---: | ---: |
| 20 statistics resets | 846185 | 731005 | 480 / 480 |
| 20 cached 64-byte physical reads | 1591525 | 1243885 | 880 / 800 |
| 20 key-only doctor lookups | 2797925 | 2057163 | 720 / 480 |
| 20 doctor/patient/history lookup triples | 13176183 | 10902903 | 3200 / 2800 |
| 240 scalar full-record rows | 32802523 | 24986224 | 14460 / 12532 |
| 240 full-record batch rows | 15478445 | 11085180 | 4148 / 3032 |
| 240 projected keys/slots | 5773307 | 4606109 | 256 / 260 |
| 30 projected doctor-prefix rows | 1013485 | 866182 | 84 / 88 |
| 20 midpoint patient lookups | 2770665 | 1917123 | 1200 / 960 |

All read phases perform zero disk writes.
For 40 two-index 128-byte inserts, scalar/bulk selections are **2240 / 716**
and T-states **44.24 M / 41.00 M**. Both perform exactly **367 writes**;
sector reads are **2420 / 2180**. This generic test removes unrelated surgery
files to leave sorting space, so its directory-read counts are not directly
comparable to the preceding fixture.
For 257 reverse-order 16-byte records with primary and normalized composite
secondary keys, ordinary insertion versus sorted creation measures:

| Path | T-states | Bank selections | Sector reads | Sector writes |
| --- | ---: | ---: | ---: | ---: |
| Incremental inserts | 759411093 | 7196 | 34161 | 10770 |
| Build/append/finish | 31624574 | 722 | 548 | 171 |

Sorted creation uses about 24 times less CPU and 98.4% fewer sector writes.
This is an explicit creation mode, not weaker durability for normal edits.
T-states exclude disk/host waiting; these are not hardware wall-clock claims.
The MEMPERF result likewise favors contiguous 4 KiB payload transfers over
many smaller calls, not transferring unnecessary bytes in control messages.

Using the actual screen loaders on a warm 20-patient page:

| Loading path | T-states | Bank selections | Record payload bytes |
| --- | ---: | ---: | ---: |
| Row-by-row lookup | 3895576 | 960 | 1820 |
| Full-record gather | 463588 | 284 | 1820 |
| Projected gather | 631074 | 156 | 800 |

Projection reduces transferred record bytes by 56% and selections by 45%
versus full gathering, at a 36% CPU packing cost for cached records. It is
still 84% faster than scalar loading, and 24% faster than the preceding
833702-T-state projected implementation. Projection is optional: full gathering
remains useful when CPU time matters more than payload size. Tests bound this
trade-off rather than claiming that projections always reduce CPU time.
All three paths perform zero disk reads/writes here. Rendering/highlighting
performs zero database lookups or physical record reads.

Relationship verification on the same fixture (including one added history
record) measures 223130787 scalar versus 67184326 batched T-states, 52324 to 2262 bank
selections, and 1127 to 994 sector reads. Both paths check all 961 links with
zero disk writes: about 70% less CPU and 96% fewer bank selections. Ordered
lookup hints and index streaming cut another 55% from the preceding batched
implementation; doctor membership checks still avoid 240 exact lookups.

### VT100 rendering

Measured on Z80ROMlessSBC against the preceding renderer, with stdout flushed
inside every measured phase so buffered output is included:

| Workload | Before T-states | After T-states | Before / after console bytes |
| --- | ---: | ---: | ---: |
| Warm 20-patient page, including its batch fetch | 2665752 | 2193239 | 1736 / 1404 |
| Patient highlight movement | 176629 | 157973 | 158 / 146 |
| History highlight movement | 450987 | 73829 | 306 / 68 |
| Warm three-entry history page, including its batch fetch | 1040233 | 805357 | 607 / 457 |
| Doctor highlight movement | 199781 | 181113 | 178 / 166 |

History highlighting uses 84% less CPU and 78% fewer output bytes. Patient and
history page rendering use 18% and 23% less CPU, respectively. Highlight changes
perform zero bank entries and disk I/O. These measurements exclude the further
savings from aligned pages: after crossing a patient-page boundary, the next
19 Down presses reuse that page; history pages likewise reuse their three rows.
No terminal scroll-region extensions or extra bank allocations are required.

## Validation

```sh
export DCC_DIR=/Users/dave/GitHub/dcc
export Z80SBC_DIR=/Users/dave/GitHub/Z80ROMlessSBC
python3 tests/test_banked.py
```

The emulator needs `z80asm`, `cpmtools` and
`/Users/dave/GitHub/Z80ROMlessSBC/requirements-bank1-system-test.txt`.
The runner uses temporary native disk images mounted before boot, not
repository disks. [FIXGEN.C](tests/FIXGEN.C) generates a small surgery fixture
once per suite using [FIXTURE.H](tests/FIXTURE.H), preserving the earlier
8-doctor/240-patient/720-history workload for performance comparisons.
[FIXDOC.C](tests/FIXDOC.C) builds DOCTOR with the same overrides, as does
[PAGETST.C](tests/PAGETST.C). Production DOCGEN and DOCTOR use the defaults in
[surgery.h](surgery.h); the overrides are test-only. The installed disk
remains unchanged.

[GENTST.C](tests/GENTST.C) runs the production record builders under the banked
emulator and checks all 6 doctors, 300 patients and 1998 history records,
including each patient's count, keys, doctor links, visit types and dates. The
suite then runs the production DOCGEN on the emulator's 512 KiB disk, requires
the complete database to fit, verifies it with the production DOCTOR and
exercises search, all 60 patients in the last roster and all 30 visits of its
last patient. Disk-fit assertions include CP/M allocation-block rounding and
the directory reservation, not just logical file byte counts.
The verified image uses 424 KiB and leaves 78 KiB free out of 502 KiB usable,
including the programs, library and extra test utilities. Full generation,
including temporary index-sort files, completes on that same image.

- [IPGTST.C](tests/IPGTST.C) runs the paging cases shared in
  [BULKTST.C](tests/BULKTST.C) separately to stay within the test program's TPA.
  It covers direct forward/backward jumps, continuation, prefixes, projections,
  key/slot modes, empty/exact/short tails, tombstones, guards and invalid requests,
  and measures equal-sized pages at different index depths. It also checks
  streamed 15-byte keys, interleaved cursors, ordered/reversed/duplicate/missing
  key batches, unchanged output on errors, and a 4000-entry index with 3998
  tombstones spanning 64 KiB. Sparse lookups have bounded comparisons and
  truncated streams report `I_EREAD`, not successful or empty results.
- [BANKTST.C](tests/BANKTST.C) uses only the native client API: generic schema
  creation, table handles, 128-byte guards, transport bounds, CRUD, metadata,
  stats, reload, corruption repair and absence of journal files.
- [NAUDIT.C](tests/NAUDIT.C) covers duplicate preflight, free-list integrity,
  interval rotation, tombstones, write-free reopen, reserved records, invalid
  metadata/free links, capacity guards and 4000 index entries crossing 64 KiB.
- [BULKTST.C](tests/BULKTST.C) covers full/tail/empty batches, projections,
  mixed scalar/bulk cursors, partial commits, cache coherence and stale lease
  witness allocations. Gathers test order, duplicate/invalid slots, guards,
  capacity and unchanged output on errors. Key batches cover primary and
  secondary keys, projections, reordered/repeated ranges, in-place caller
  buffers, malformed contracts and stale leases. Guards verify both 4 KiB
  and short-tail output.
- [PAGETST.C](tests/PAGETST.C) invokes the real DOCTOR loaders/renderers.
  Rendering regressions measure flushed output bytes and CPU, check highlight
  attributes, blank tail rows and long-word wrapping, and require no database
  calls for highlight changes. Interactive tests cross patient/history pages
  in both directions and check that navigation inside each new page stays local.
  It grows the directory to 45 doctors, verifies all relationships, and proves
  that a late page reads only its five requested rows without scanning earlier
  pages or depending on the generator's doctor count.
  It checks page boundaries, arbitrary search order, history paging, cache
  refresh after mutation/nested screens, local-only highlighting and measured
  scalar/full/projected page loading. It also compares scalar/batched
  relationship verification and rejects missing or mismatched references.
  A guarded 2560-byte workspace loads all 999 visits and rejects excess rows.
- [OPTST.C](tests/OPTST.C) covers composite normalization, nonzero tail
  offsets, malformed schemas, one-byte projected scans, sorted-run merging,
  cache retention across durable writes, duplicate/interrupted build isolation,
  and building/rebuilding 4000 active keys across the 64 KiB boundary.
- [BPERF.C](tests/BPERF.C) measures 13 read workloads and scalar/bulk insertion.
  [test_banked.py](tests/test_banked.py) enforces size, CPU, bank traffic,
  sector-read and unchanged durability-write budgets.
- Integration regenerates surgery data, verifies all relationships, checks
  nested UI/stack behavior and missing-library/data-file failures, and confirms
  that database writes did not overwrite the library.
