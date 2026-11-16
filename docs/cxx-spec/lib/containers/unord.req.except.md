# 22.2.7.1 Exception safety guarantees [unord.req.except]

[Contents](../../README.md) · up: [22.2.7 Unordered associative containers \[unord.req\]](unord.req.md) · prev: [22.2.7 Unordered associative containers \[unord.req\]](unord.req.md) · next: [22.3 Sequence containers \[sequences\]](sequences.md)

**1** For unordered associative containers, no `clear()` function throws an exception. `erase(k)` does not throw an exception unless that exception is thrown by the container’s `Hash` or `Pred` object (if any).

**2** For unordered associative containers, if an exception is thrown by any operation other than the container’s hash function from within an `insert` or `emplace` function inserting a single element, the insertion has no effect.

**3** For unordered associative containers, no `swap` function throws an exception unless that exception is thrown by the swap of the container’s `Hash` or `Pred` object (if any).

**4** For unordered associative containers, if an exception is thrown from within a `rehash()` function other than by the container’s hash function or comparison function, the `rehash()` function has no effect.
