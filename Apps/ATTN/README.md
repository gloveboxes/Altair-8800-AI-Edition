# ATTN — "Paper Tape is All You Need"

A tiny **single-layer, single-head transformer** that learns to **reverse an
8-digit sequence**, running on the Altair 8800 / CP/M. It trains itself from
scratch in a couple of minutes of emulated Z80 time, then reverses digit
strings you feed it — a complete attention-based neural network in a few
kilobytes of CP/M memory.

```text
 1 2 3 4 5 6 7 8 -> 8 7 6 5 4 3 2 1  ok
 8 6 4 2 0 1 3 9 -> 9 3 1 0 2 4 6 8  ok
```

## Lineage

This app is a port-of-a-port, and the credit belongs upstream:

- **Damien Boureille — [`dbrll/ATTN-11`](https://github.com/dbrll/ATTN-11)**
  ("ATTN/11 — Paper Tape Is All You Need"). The original: a 1-layer, 1-head
  transformer hand-written in PDP-11 assembly on top of **NN11**, a minimal
  fixed-point neural-network stack. It trains on a real PDP-11/34A.
- **Dave Plummer — [`davepl/pdpsrc`](https://github.com/davepl/pdpsrc/tree/main/bsd/attn)**
  ported ATTN/11 to the 2.11BSD `as` assembler, and covers the project in his
  YouTube video **["Paper Tape is All You Need"](https://youtu.be/OUE3FSIk46g)**.
- **This repo** brings it to the Altair 8800 / CP/M: first as BDS C, then as an
  optimized dcc C11 program, plus a Z80 assembly inference port.

## The model

An encoder-only transformer: embedding → self-attention → residual → output
projection → softmax. No layer norm, no feed-forward network, no decoder — for
the reversal task, attention plus a residual connection is enough.

| Property        | Value              |
| --------------- | ------------------ |
| Layers          | 1                  |
| Heads           | 1                  |
| d_model         | 16                 |
| Sequence length | 8                  |
| Vocabulary      | 10 (digits 0–9)    |
| Parameters      | 1,216              |

Reversal is a deliberately non-trivial target: the network has to route each
token to a position that depends only on its index, with no content-based
shortcut — exactly the kind of problem self-attention is built for.

### Fixed-point numerics

The 8080/Z80 has no floating-point unit, so everything is fixed-point, matching
the NN11 scheme:

| Pass | Format | Meaning |
| --- | --- | --- |
| Forward activations | Q8 | 8 fractional bits (1/256) |
| Backward gradients | Q15 | 15 fractional bits |
| Weight accumulators | Q16 | 32-bit (16.16 fixed point) |

Transcendentals are replaced by lookup tables: `exptbl` (256 entries, Q8) maps
`i → exp(-i/32)` for the softmax, and `logtbl` (Q12) maps `x → -ln(x/256)·4096`
for the cross-entropy loss used to monitor convergence. The softmax/cross-entropy
gradient reduces to `dL = softmax(logits) - one_hot(target)`, so no logarithm is
needed during training.

## Files

| File | What it is |
| --- | --- |
| `attnc11.c` | The C11 implementation for the **dcc** compiler (CP/M 2.2 / Z80). Trains *and* infers. This is the recommended version. |
| `ATTN.C` | The earlier **BDS C 1.6** implementation. Functionally equivalent; uses the BDS C `LONG` package for 32-bit math. Kept for reference. |
| `ATTNZ80.MAC` | A **Z80 (M80/L80) inference-only** port. Loads trained weights and reverses sequences; does not train. |
| `ATTNZ80.SUB` | CP/M `SUBMIT` script that assembles `ATTNZ80.MAC` with M80/L80. |
| `ATTN.IN` | Sample input: one 8-digit sequence per line. |
| `ATTN.WTS` | Trained weights produced by `ATTNC11 -t`, reloaded for inference. |

`attnc11.c` is a from-scratch optimization of `ATTN.C` for the dcc toolchain:
because dcc has a native 32-bit `long`, the hot fixed-point routines (`vdot`,
`mvmul`, `vtmul`, …) use ordinary arithmetic instead of the BDS C `LONG`
function-call package, so it is both smaller and considerably faster.

The C11 port also uses dcc's one-byte `bool`, pointer induction, fixed-shape
attention kernels, and an all-row Q/K/V pass that reuses each input activation
across the three projections. The score dot product and scaling are fused, and
the 8x16 attention-value and 16x10 output projections avoid generic dimension
handling. These transformations preserve the fixed-point accumulation order;
training from the same seed produces a byte-identical `ATTN.WTS`.

Internal state and helper names are expanded for readability instead of
retaining the seven-character identifiers required by the original BDS C port.
All file-local state has internal linkage, so descriptive names cannot collide
at the CP/M linker boundary.

Measured with `ntvcm -p` over the 14 sequences in `ATTN.IN`, the optimized
forward path uses 442,506,230 Z80 cycles versus 455,257,575 before the
inference-specific pass, a 2.80% reduction. The speed-focused kernels increase
`ATTNC11.COM` from 16,768 to 18,560 bytes. C11 features that dcc only parses for
source compatibility, such as `restrict`, are deliberately not presented as
target optimizations.

## Usage

```text
attnc11          reverse each 8-digit line in ATTN.IN
attnc11 <file>   reverse each line in <file>
attnc11 -t       train, save weights to ATTN.WTS, then self-test
attnc11 -h       help
```

Run `attnc11 -t` once to train and create `ATTN.WTS`, then run `attnc11` (or
`attnz80`) for inference. Training converges quickly — the run stops early
once it reaches 100% validation accuracy on `ATTN.IN`.

## Building

**C11 trainer/inference (`attnc11.c`)** with the dcc toolchain:

```sh
./build-app.sh             # -> ATTNC11.COM
ntvcm ATTNC11.COM -t       # train + self-test under the emulator
```

**Z80 inference port (`ATTNZ80.MAC`)** with M80/L80:

```sh
m80 attnz80=attnz80
l80 attnz80,attnz80/n/e   # -> ATTNZ80.COM
```

Both consume the same `ATTN.WTS` weight file, so you can train once with
`ATTNC11 -t` and run inference with either build.
