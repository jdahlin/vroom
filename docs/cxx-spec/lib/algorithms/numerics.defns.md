# 25.10.1 Definitions [numerics.defns]

[Contents](../../README.md) · up: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · prev: [25.10 Generalized numeric operations \[numeric.ops\]](numeric.ops.md) · next: [25.10.2 Accumulate \[accumulate\]](accumulate.md)

**1** Define `GENERALIZED_NONCOMMUTATIVE_SUM(op, a1, ..., aN)` as follows:

- `a1` when `N` is `1`, otherwise
- `op(GENERALIZED_NONCOMMUTATIVE_SUM(op, a1, ..., aK),` <br> `GENERALIZED_NONCOMMUTATIVE_SUM(op, aM, ..., aN))` for any `K` where 1 &lt; `K`+1 = `M` ≤ `N`.

**2** Define `GENERALIZED_SUM(op, a1, ..., aN)` as `GENERALIZED_NONCOMMUTATIVE_SUM(op, b1, ..., bN)`, where `b1, ..., bN` may be any permutation of `a1, ..., aN`.
