# 31.4 Order and consistency [atomics.order]

[Contents](../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31.3 Type aliases \[atomics.alias\]](atomics.alias.md) · next: [31.5 Lock-free property \[atomics.lockfree\]](atomics.lockfree.md)

```cpp
namespace std {
  enum class memory_order : unspecified {
    relaxed, consume, acquire, release, acq_rel, seq_cst
  };
  inline constexpr memory_order memory_order_relaxed = memory_order::relaxed;
  inline constexpr memory_order memory_order_consume = memory_order::consume;
  inline constexpr memory_order memory_order_acquire = memory_order::acquire;
  inline constexpr memory_order memory_order_release = memory_order::release;
  inline constexpr memory_order memory_order_acq_rel = memory_order::acq_rel;
  inline constexpr memory_order memory_order_seq_cst = memory_order::seq_cst;
}
```

**1** The enumeration `memory_order` specifies the detailed regular (non-atomic) memory synchronization order as defined in [[intro.multithread]](../intro.multithread.md) and may provide for operation ordering. Its enumerated values and their meanings are as follows:

- `memory_order::relaxed`: no operation orders memory.
- `memory_order::release`, `memory_order::acq_rel`, and `memory_order::seq_cst`: a store operation performs a release operation on the affected memory location.
- `memory_order::consume`: a load operation performs a consume operation on the affected memory location. [*Note 1*: Prefer `memory_order::acquire`, which provides stronger guarantees than `memory_order::consume`. Implementations have found it infeasible to provide performance better than that of `memory_order::acquire`. Specification revisions are under consideration. — *end note*]
- `memory_order::acquire`, `memory_order::acq_rel`, and `memory_order::seq_cst`: a load operation performs an acquire operation on the affected memory location.

[*Note 2*: Atomic operations specifying `memory_order::relaxed` are relaxed with respect to memory ordering. Implementations must still guarantee that any given atomic access to a particular atomic object be indivisible with respect to all other atomic accesses to that object. — *end note*]

**2** An atomic operation *A* that performs a release operation on an atomic object *M* synchronizes with an atomic operation *B* that performs an acquire operation on *M* and takes its value from any side effect in the release sequence headed by *A*.

**3** An atomic operation *A* on some atomic object *M* is *coherence-ordered before* another atomic operation *B* on *M* if

- *A* is a modification, and *B* reads the value stored by *A*, or
- *A* precedes *B* in the modification order of *M*, or
- *A* and *B* are not the same atomic read-modify-write operation, and there exists an atomic modification *X* of *M* such that *A* reads the value stored by *X* and *X* precedes *B* in the modification order of *M*, or
- there exists an atomic modification *X* of *M* such that *A* is coherence-ordered before *X* and *X* is coherence-ordered before *B*.

**4** There is a single total order *S* on all `memory_order::seq_cst` operations, including fences, that satisfies the following constraints. First, if *A* and *B* are `memory_order::seq_cst` operations and *A* strongly happens before *B*, then *A* precedes *B* in *S*. Second, for every pair of atomic operations *A* and *B* on an object *M*, where *A* is coherence-ordered before *B*, the following four conditions are required to be satisfied by *S*:

- if *A* and *B* are both `memory_order::seq_cst` operations, then *A* precedes *B* in *S*; and
- if *A* is a `memory_order::seq_cst` operation and *B* happens before a `memory_order::seq_cst` fence *Y*, then *A* precedes *Y* in *S*; and
- if a `memory_order::seq_cst` fence *X* happens before *A* and *B* is a `memory_order::seq_cst` operation, then *X* precedes *B* in *S*; and
- if a `memory_order::seq_cst` fence *X* happens before *A* and *B* happens before a `memory_order::seq_cst` fence *Y*, then *X* precedes *Y* in *S*.

**5** [*Note 3*: This definition ensures that *S* is consistent with the modification order of any atomic object *M*. It also ensures that a `memory_order::seq_cst` load *A* of *M* gets its value either from the last modification of *M* that precedes *A* in *S* or from some non-`memory_order::seq_cst` modification of *M* that does not happen before any modification of *M* that precedes *A* in *S*. — *end note*]

**6** [*Note 4*: We do not require that *S* be consistent with “happens before” ([[intro.races]](../intro.races.md)). This allows more efficient implementation of `memory_order::acquire` and `memory_order::release` on some machine architectures. It can produce surprising results when these are mixed with `memory_order::seq_cst` accesses. — *end note*]

**7** [*Note 5*: `memory_order::seq_cst` ensures sequential consistency only for a program that is free of data races and uses exclusively `memory_order::seq_cst` atomic operations. Any use of weaker ordering will invalidate this guarantee unless extreme care is used. In many cases, `memory_order::seq_cst` atomic operations are reorderable with respect to other atomic operations performed by the same thread. — *end note*]

**8** Implementations should ensure that no “out-of-thin-air” values are computed that circularly depend on their own computation.

[*Note 6*: For example, with `x` and `y` initially zero,

```cpp
// Thread 1:
r1 = y.load(memory_order::relaxed);
x.store(r1, memory_order::relaxed);
```

```cpp
// Thread 2:
r2 = x.load(memory_order::relaxed);
y.store(r2, memory_order::relaxed);
```

should not produce `r1 == r2 == 42`, since the store of 42 to `y` is only possible if the store to `x` stores `42`, which circularly depends on the store to `y` storing `42`. Note that without this restriction, such an execution is possible. — *end note*]

**9** [*Note 7*: The recommendation similarly disallows `r1 == r2 == 42` in the following example, with `x` and `y` again initially zero:

```cpp
// Thread 1:
r1 = x.load(memory_order::relaxed);
if (r1 == 42) y.store(42, memory_order::relaxed);
```

```cpp
// Thread 2:
r2 = y.load(memory_order::relaxed);
if (r2 == 42) x.store(42, memory_order::relaxed);
```

— *end note*]

**10** Atomic read-modify-write operations shall always read the last value (in the modification order) written before the write associated with the read-modify-write operation.

**11** Implementations should make atomic stores visible to atomic loads within a reasonable amount of time.

```cpp
template<class T>
  T kill_dependency(T y) noexcept;
```

**12** *Effects:* The argument does not carry a dependency to the return value ([[intro.multithread]](../intro.multithread.md)).

**13** *Returns:* `y`.
