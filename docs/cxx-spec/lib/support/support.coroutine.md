# 17.12 Coroutines [support.coroutine]

[Contents](../../README.md) · up: [17 Language support library \[support\]](support.md) · prev: [17.11.6 Comparison algorithms \[cmp.alg\]](cmp.alg.md) · next: [17.12.1 Header `<coroutine>` synopsis \[coroutine.syn\]](coroutine.syn.md)

**1** The header `<coroutine>` defines several types providing compile and run-time support for coroutines in a C++ program.

## Subclauses

- [17.12.1 Header `<coroutine>` synopsis \[coroutine.syn\]](coroutine.syn.md)
- [17.12.2 Coroutine traits \[coroutine.traits\]](coroutine.traits.md)
  - [17.12.2.1 Class template `coroutine_traits` \[coroutine.traits.primary\]](coroutine.traits.primary.md)
- [17.12.3 Class template `coroutine_handle` \[coroutine.handle\]](coroutine.handle.md)
  - [17.12.3.1 Construct/reset \[coroutine.handle.con\]](coroutine.handle.con.md)
  - [17.12.3.2 Export/import \[coroutine.handle.export.import\]](coroutine.handle.export.import.md)
  - [17.12.3.3 Observers \[coroutine.handle.observers\]](coroutine.handle.observers.md)
  - [17.12.3.4 Resumption \[coroutine.handle.resumption\]](coroutine.handle.resumption.md)
  - [17.12.3.5 Promise access \[coroutine.handle.promise\]](coroutine.handle.promise.md)
  - [17.12.3.6 Comparison operators \[coroutine.handle.compare\]](coroutine.handle.compare.md)
  - [17.12.3.7 Hash support \[coroutine.handle.hash\]](coroutine.handle.hash.md)
- [17.12.4 No-op coroutines \[coroutine.noop\]](coroutine.noop.md)
  - [17.12.4.1 Class `noop_coroutine_promise` \[coroutine.promise.noop\]](coroutine.promise.noop.md)
  - [17.12.4.2 Class `coroutine_handle<noop_coroutine_promise>` \[coroutine.handle.noop\]](coroutine.handle.noop.md)
    - [17.12.4.2.1 Observers \[coroutine.handle.noop.observers\]](coroutine.handle.noop.observers.md)
    - [17.12.4.2.2 Resumption \[coroutine.handle.noop.resumption\]](coroutine.handle.noop.resumption.md)
    - [17.12.4.2.3 Promise access \[coroutine.handle.noop.promise\]](coroutine.handle.noop.promise.md)
    - [17.12.4.2.4 Address \[coroutine.handle.noop.address\]](coroutine.handle.noop.address.md)
  - [17.12.4.3 Function `noop_coroutine` \[coroutine.noop.coroutine\]](coroutine.noop.coroutine.md)
- [17.12.5 Trivial awaitables \[coroutine.trivial.awaitables\]](coroutine.trivial.awaitables.md)
