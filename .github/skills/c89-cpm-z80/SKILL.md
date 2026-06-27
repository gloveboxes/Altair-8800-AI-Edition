---
name: c89-cpm-z80
description: 'Write, build, test, and debug C89 code for the dcc compiler targeting CP/M 2.2 on the Z80 (run under the ntvcm Altair 8800 emulator). Use for .c/.h sources compiled with dcc, or tasks mentioning dcc, CP/M, CP/M 2.2, Z80, ntvcm, DCCRTL, ma.sh, or VT100/ANSI CP/M terminal apps. Treat dcc as standard C89 plus a few C99 conveniences (for-init decls, // comments, block scope, inline-ignored) EXCEPT for the deviations this skill documents: no double (32-bit float is the only floating type), 16-bit int/short/pointer/size_t, 32-bit long, signed char, and a nearly complete C89 library whose floating and CP/M-dependent calls are limited (strtod/atof return float not double; time/signal/locale exist but are CP/M no-op stubs). Full library/printf/scanf inventory and pitfalls are in the reference files.'
argument-hint: 'Describe the C89/CP-M task (write code, build, run under ntvcm, debug a failure)'
---

# C89 for dcc (CP/M 2.2 / Z80)

dcc is a cross-compiler (runs on the host) that emits Z80 assembly for CP/M 2.2.
The runtime is [DCCRTL.MAC](DCCRTL.MAC); programs run on real hardware or an
emulator such as **ntvcm** (Altair 8800).

**Assume standard C89 plus the C99 conveniences listed below.** This skill
documents only where dcc *deviates* from what an experienced C programmer
expects — anything not listed here behaves as standard C89/C99.

## When to use

- Writing, porting, or reviewing C compiled by `dcc`.
- Building/running/debugging a dcc program (`ma.sh`, `ntvcm`).
- CP/M file I/O, VT100/ANSI console UIs, or DCCRTL work.

## Deviations from standard C

**Types — a 16-bit machine:**

| Type | dcc | Note |
| ---- | --- | ---- |
| `int` / `short` | 16-bit | overflow at ±32767; use `long`+`%ld` (needs `-flongio`) for range |
| `long` | 32-bit | |
| `float` | 32-bit | **the only floating type** |
| `double` / `long double` | — | **not a keyword; using it is a compile error** |
| pointer / `size_t` / `ptrdiff_t` / `wchar_t` | 16-bit | flat 64 KB space |
| `char` | 8-bit **signed** | use `unsigned char` for bytes ≥ 0x80 / table indices |
| `FILE` | `int` | |

Multi-byte values are little-endian (Z80-native).

**Floating point is single-precision only:**

- Write `float`; unsuffixed constants (`3.14`) are already `float`, not `double`.
- No `float`→`double` promotion in varargs (there is no double), so
  `printf("%f", x)` consumes a 32-bit `float` directly — but **requires the
  `-ffloatio` build flag** (`ma.sh` adds it automatically when it sees a `%f` in
  the source); without it `%f` silently does nothing.
- `<math.h>` provides the full single-precision set (`sinf`/`expf`/`powf`/… each
  with an unsuffixed alias that stays single-precision), but the transcendentals
  are ~5–6-digit polynomial approximations.
- `atof` returns `float` (not `double`); `strtod` also exists but likewise
  returns `float` (it delegates to `atof` and sets `endptr`).

**The C89 library is essentially complete — assume a standard function is
present.** Any standard C89 call (`strtod`, `fgetc`, `ungetc`, `rename`,
`realloc`, `qsort`, `bsearch`, `setjmp`, `vprintf`, the `mb*`/`wc*` set, …) is
implemented and linkable; a genuinely missing one is a **link** error
(`unresolved external`), not a compile error (see
[references/library.md](./references/library.md)). The only exceptions, all
forced by CP/M 2.2 or the single-precision float, are:

- `<time.h>`: no real-time clock, so `clock`/`time`/`mktime` return `-1` and the
  broken-down-time calls (`localtime`/`gmtime`/`asctime`/`ctime`/`strftime`)
  return `NULL`/`0`.
- `<signal.h>`: `signal`/`raise` are no-ops — only `raise(SIGABRT)` calls
  `abort()`.
- `<locale.h>`: `setlocale` accepts only `"C"`.
- `strtod`/`atof` return **`float`** (there is no `double`); use
  `strtol`/`strtoul` for integers.

(dcc uses abbreviated public symbols internally — e.g. `realloc`→`__real`,
`strcoll`→`__scol`, `fgetc`→`__fgetc` — so grepping `_funcname` in DCCRTL.MAC
can under-report what is actually linkable.)

**printf/scanf are a subset.** Field width, `-` left-justify, and integer
precision work, but there are no `+`/space/`#` flags and no `*` run-time
width/precision. Two format families are **opt-in build flags**: `%f` needs
`-ffloatio` and the long formats (`%ld`/`%lu`/`%lx`/`%lX`/`%ls`) need `-flongio`
— without the flag that specifier silently produces nothing. `ma.sh` scans the
source and adds whichever flag it sees, so this is usually automatic. scanf is
integer/string only (no `%f`, scansets, `%n`, `%p`). Conversion tables in
library.md.

**No stack/heap guard by default.** Heap and stack share memory and can collide
silently. Size the stack with `-stack N` (default 512); keep big buffers
`static`/global. For deep/recursive code, opt into the lightweight guard with
`-fstack-check`, which aborts gracefully on overflow instead of corrupting the
heap (`ma.sh` enables it when the source contains a `DCC_STACK_CHECK` marker or
you set `DCC_FORCE_STACK_CHECK=1`).

**Source filenames MUST be 8.3 and uppercase-safe** (≤ 8-char base, ≤ 3-char
extension, no extra dots). `foo.c` → `FOO.COM`, run as `ntvcm FOO`. A source
whose name violates 8.3 (e.g. `my_long_name.c`, `parse.test.c`) won't build —
ntvcm reports `argument is not a valid CP/M 8.3 filename`; rename the file when
you see that error.

**Missing `<...>` headers are silently ignored** — calls fall back to implicit
`int` and still link via the runtime, with no type-checking. A missing
`"..."` header is fatal. If standard calls compile but misbehave, check that
`-I` actually resolves the dcc headers.

## C99 conveniences dcc accepts (beyond C89)

These behave as standard C99: `for`-init declarations with loop scope, `//` line
comments, block-scoped declarations (inner blocks shadow outer names), and
`inline` (a **valid** keyword that compiles cleanly, but the compiler ignores it
— **no inline optimization is implemented**; every function emits a real Z80
`call`, so hand-inline tiny helpers in hot loops when you need the speed).
`const`/`volatile`/`register`/`auto` are accepted
but inert (`const` constant-folds initializers only — not read-only memory).
K&R function definitions are still accepted; prefer prototypes for new code.

Not present: any other C99/C11 feature (`restrict`, `_Bool`, VLAs, compound
literals, designated initializers).

**Identifiers:** full internal significance; externals stay distinct well past
C89's 6-char minimum (verified to ~13 chars), and only ~16+ identical leading
characters can silently collide at link time — make such a one-file helper
`static` if it ever matters. (This is *not* BDS C's 7-char rule.)

## Build and run

The standard helper scripts live in the dcc repo. **Set these env vars first**
(they make `ma.sh` use the local binaries):

```sh
export PATH="/Users/<USER_NAME_FOLDER>/GitHub/ntvcm:/Users/<USER_NAME_FOLDER>/GitHub/dcc:$PATH"
export DCC=./dcc DCCPEEP=./dccpeep DCCRTLSTRIP=./dccrtlstrip
```

**Build/run one program** (compile → peephole → strip runtime → M80 → L80):

```sh
./ma.sh foo peep      # foo.c -> FOO.COM (peephole optimised); use 'nopeep' to skip
ntvcm FOO             # run it (uppercase, no extension)
ntvcm FOO ARG1 ARG2   # with CP/M command-line args
```

> The source name passed to `ma.sh` must be 8.3-clean (base ≤ 8 chars,
> extension ≤ 3, no extra dots). ntvcm reports
> `argument is not a valid CP/M 8.3 filename` for a non-conforming name —
> rename the file when you see it.

**Useful `dcc` options:** `-o file` (output .mac), `-c`/`-module` (linkable
module), `-f`/`-ffloatio` (float `%f` printf), `-fl`/`-flongio` (long
`%ld`/`%lu`/`%lx`/`%lX`/`%ls` printf), `-fstack-check` (opt-in graceful
stack-overflow abort), `-stack N`/`-s N`/`--stack N` (reserve stack; default
512 — heap and stack share memory, no guard unless `-fstack-check`), `-I dir`
(or joined `-Idir`; repeatable), `-Dname[=v]`,
`-Uname`, `-v`, `-h`. `_DCC_=1` is always predefined. Unrecognised flags are
silently ignored (a typo'd flag is a no-op, not an error).

**Finding the standard headers (`-I`).** dcc resolves `#include <stdio.h>` by
checking the current directory first, then each `-I` directory in order. The
bundled headers (`stdio.h`, `stdlib.h`, `string.h`, `math.h`, …) live in the
**dcc repo root**, so:

- Building **inside** the dcc repo (as `ma.sh` does): they're found
  automatically via the current directory — no `-I` needed.
- Building **elsewhere**: point dcc at the repo, e.g.
  `dcc -I /path/to/dcc myapp.c -o myapp.mac` (repeat `-I` for more dirs).

Gotcha: a `<...>` header that isn't found is **silently ignored** (you lose its
prototypes and fall back to implicit `int`, so calls still compile and link via
the runtime but without type-checking); a missing `"..."` header is a fatal
error. If standard calls compile yet misbehave, check that `-I` actually
resolves the dcc headers.

Notes: M80 needs CRLF (`ma.sh` converts). `RTLMIN.MAC` is generated per-app by
`dccrtlstrip` during the build — don't hand-edit it.

## Performance on the Z80

The compiler does little optimisation beyond an optional peephole pass, and the
8080/Z80 has no cache and slow multiply/divide, so source-level choices matter:

- **Replace element-wise copy/clear loops with `memcpy`/`memset`.** DCCRTL
  implements them with the Z80 block instructions (`LDIR`/`LDIR`-style fills),
  which are far faster and smaller than a C `for` loop assigning one element at
  a time. Use them for any array/struct copy or zero-fill on a hot path.
- **There is no inlining** (see above) — every helper call is a real `CALL`/
  `RET`. Hand-inline tiny helpers (a comparison, an index calc) inside the
  innermost loops of hot code; keep them as functions everywhere else.
- **Avoid `*`, `/`, `%` in hot loops.** 16-bit multiply/divide/modulo are
  library subroutine calls, not single instructions. Prefer addition, shifts
  (`<<`/`>>` for powers of two), or precomputed values.
- **Buffer console output.** Each `putchar` is otherwise one BDOS call. Install
  one static buffer once with `setvbuf(stdout, buf, _IOFBF, N)`, write through
  `putchar`-based escape helpers, and `fflush(stdout)` only where the user must
  see output (before a key read, during a pause). This collapses many tiny
  writes into a few BDOS calls.
- **Share one pointer-taking routine instead of duplicating logic.** A function
  that takes `T *` works for both a global and a local buffer, so you don't pay
  code size (and bugs) for near-identical copies.
- **Recursion needs stack headroom.** Stack and heap share one region with no
  guard by default; deep/recursive code silently corrupts the heap on overflow.
  Raise it with `-stack N` (default 512), opt into `-fstack-check` while tuning
  to catch an overflow gracefully, and keep large scratch buffers
  `static`/global rather than on the stack.

## Top pitfalls

The deviations above are the pitfalls. For worked examples (the `float` decimal
parser, `%f`/`-ffloatio`, 16-bit overflow, signed `char`, CP/M 8.3 names, the
stack/heap collision) see [references/pitfalls.md](./references/pitfalls.md);
for the full function inventory and `printf`/`scanf` conversion tables see
[references/library.md](./references/library.md).

## Workflow

1. **Plan for the deviations.** Floating point → single precision (no `double`);
   decimal parsing → `atof`/`strtod` return `float`, not `double`;
   `time`/`signal`/`locale` link but are CP/M no-op stubs.
2. **Check the library** in [references/library.md](./references/library.md)
   before calling anything unverified — a missing function is a link error,
   not a compile error.
3. **Match repo conventions.** Read a nearby working program first. In the dcc
   repo, the exhaustive reference is
   [dcc-c89-reference-guide.md](dcc-c89-reference-guide.md) at the repo root.
4. **Build and run**: `./ma.sh <name> peep && ntvcm <NAME>`. `ma.sh` scans the
   source and auto-enables `-ffloatio` (for `%f`) and `-flongio` (for `%ld`/…),
   and turns on `-fstack-check` if the source has a `DCC_STACK_CHECK` marker;
   redirect stdin for interactive apps and compare against expected output.
