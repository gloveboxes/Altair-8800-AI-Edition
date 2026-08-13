# DCC Bugs Exposed by CALC

## Large finite `%f` values saturate

DCCRTL's `%f` formatter prints some large finite `float` values as `4294967295` instead of their decimal value. This was observed with the exactly representable value $2^{32}$.

Expected:

```text
4294967296
```

Actual:

```text
4294967295
```

CALC worked around this by avoiding the native float formatter, and later removed its native float path entirely.

## `powf` returns zero for a domain error

`powf(-2.0f, 0.5f)` returns `0` rather than reporting the invalid operation with NaN or another detectable domain-error result.

Expected: a NaN or detectable domain error.

Actual:

```text
0
```

CALC worked around this by rejecting negative bases with non-integer exponents before calling `powf`.

## Valid struct-heavy power loop is rejected

DCC rejected a valid exponentiation-by-squaring implementation that combined structure assignments and conditional arithmetic calls inside a `while` loop. The compiler reported an unsupported loop condition or body instead of compiling the C code.

CALC worked around this by replacing the loop with simpler bounded repeated multiplication.
