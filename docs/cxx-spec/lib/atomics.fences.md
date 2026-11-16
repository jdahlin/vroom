# 31.11 Fences [atomics.fences]

[Contents](../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31.10 Flag type and operations \[atomics.flag\]](atomics.flag.md) · next: [32 Thread support library \[thread\]](thread.md)

**1** This subclause introduces synchronization primitives called *fences*. Fences can have acquire semantics, release semantics, or both. A fence with acquire semantics is called an *acquire fence*. A fence with release semantics is called a *release fence*.

**2** A release fence *A* synchronizes with an acquire fence *B* if there exist atomic operations *X* and *Y*, both operating on some atomic object *M*, such that *A* is sequenced before *X*, *X* modifies *M*, *Y* is sequenced before *B*, and *Y* reads the value written by *X* or a value written by any side effect in the hypothetical release sequence *X* would head if it were a release operation.

**3** A release fence *A* synchronizes with an atomic operation *B* that performs an acquire operation on an atomic object *M* if there exists an atomic operation *X* such that *A* is sequenced before *X*, *X* modifies *M*, and *B* reads the value written by *X* or a value written by any side effect in the hypothetical release sequence *X* would head if it were a release operation.

**4** An atomic operation *A* that is a release operation on an atomic object *M* synchronizes with an acquire fence *B* if there exists some atomic operation *X* on *M* such that *X* is sequenced before *B* and reads the value written by *A* or a value written by any side effect in the release sequence headed by *A*.

```cpp
extern "C" void atomic_thread_fence(memory_order order) noexcept;
```

**5** *Effects:* Depending on the value of `order`, this operation:

- has no effects, if `order == memory_order::relaxed`;
- is an acquire fence, if `order == memory_order::acquire` or `order == memory_order::consume`;
- is a release fence, if `order == memory_order::release`;
- is both an acquire fence and a release fence, if `order == memory_order::acq_rel`;
- is a sequentially consistent acquire and release fence, if `order == memory_order::seq_cst`.

```cpp
extern "C" void atomic_signal_fence(memory_order order) noexcept;
```

**6** *Effects:* Equivalent to `atomic_thread_fence(order)`, except that the resulting ordering constraints are established only between a thread and a signal handler executed in the same thread.

**7** [*Note 1*: `atomic_signal_fence` can be used to specify the order in which actions performed by the thread become visible to the signal handler. Compiler optimizations and reorderings of loads and stores are inhibited in the same way as with `atomic_thread_fence`, but the hardware fence instructions that `atomic_thread_fence` would have inserted are not emitted. — *end note*]
