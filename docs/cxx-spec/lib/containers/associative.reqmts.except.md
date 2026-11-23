# 22.2.6.1 Exception safety guarantees [associative.reqmts.except]

[Contents](../../README.md) · up: [22.2.6 Associative containers \[associative.reqmts\]](associative.reqmts.md) · prev: [22.2.6 Associative containers \[associative.reqmts\]](associative.reqmts.md) · next: [22.2.7 Unordered associative containers \[unord.req\]](unord.req.md)

**1** For associative containers, no `clear()` function throws an exception. `erase(k)` does not throw an exception unless that exception is thrown by the container’s `Compare` object (if any).

**2** For associative containers, if an exception is thrown by any operation from within an `insert` or `emplace` function inserting a single element, the insertion has no effect.

**3** For associative containers, no `swap` function throws an exception unless that exception is thrown by the swap of the container’s `Compare` object (if any).
