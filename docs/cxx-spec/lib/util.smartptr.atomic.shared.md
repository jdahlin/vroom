# 31.8.6.1 Partial specialization for `shared_ptr` [util.smartptr.atomic.shared]

[Contents](../README.md) · up: [31.8.6 Partial specializations for smart pointers \[util.smartptr.atomic\]](util.smartptr.atomic.md) · prev: [31.8.6 Partial specializations for smart pointers \[util.smartptr.atomic\]](util.smartptr.atomic.md) · next: [31.8.6.2 Partial specialization for `weak_ptr` \[util.smartptr.atomic.weak\]](util.smartptr.atomic.weak.md)

```cpp
namespace std {
  template<class T> struct atomic<shared_ptr<T>> {
    using value_type = shared_ptr<T>;

    static constexpr bool is_always_lock_free = implementation-defined;
    bool is_lock_free() const noexcept;

    constexpr atomic() noexcept;
    atomic(shared_ptr<T> desired) noexcept;
    atomic(const atomic&) = delete;
    void operator=(const atomic&) = delete;

    shared_ptr<T> load(memory_order order = memory_order::seq_cst) const noexcept;
    operator shared_ptr<T>() const noexcept;
    void store(shared_ptr<T> desired, memory_order order = memory_order::seq_cst) noexcept;
    void operator=(shared_ptr<T> desired) noexcept;

    shared_ptr<T> exchange(shared_ptr<T> desired,
                           memory_order order = memory_order::seq_cst) noexcept;
    bool compare_exchange_weak(shared_ptr<T>& expected, shared_ptr<T> desired,
                               memory_order success, memory_order failure) noexcept;
    bool compare_exchange_strong(shared_ptr<T>& expected, shared_ptr<T> desired,
                                 memory_order success, memory_order failure) noexcept;
    bool compare_exchange_weak(shared_ptr<T>& expected, shared_ptr<T> desired,
                               memory_order order = memory_order::seq_cst) noexcept;
    bool compare_exchange_strong(shared_ptr<T>& expected, shared_ptr<T> desired,
                                 memory_order order = memory_order::seq_cst) noexcept;

    void wait(shared_ptr<T> old, memory_order order = memory_order::seq_cst) const noexcept;
    void notify_one() noexcept;
    void notify_all() noexcept;

  private:
    shared_ptr<T> p;            // exposition only
  };
}
```

```cpp
constexpr atomic() noexcept;
```

**1** *Effects:* Initializes `p{}`.

```cpp
atomic(shared_ptr<T> desired) noexcept;
```

**2** *Effects:* Initializes the object with the value `desired`. Initialization is not an atomic operation ([[intro.multithread]](../intro.multithread.md)). [*Note 1*: It is possible to have an access to an atomic object `A` race with its construction, for example, by communicating the address of the just-constructed object `A` to another thread via `memory_order::relaxed` operations on a suitable atomic pointer variable, and then immediately accessing `A` in the receiving thread. This results in undefined behavior. — *end note*]

```cpp
void store(shared_ptr<T> desired, memory_order order = memory_order::seq_cst) noexcept;
```

**3** *Preconditions:* `order` is neither `memory_order::consume`, `memory_order::acquire`, nor `memory_order::acq_rel`.

**4** *Effects:* Atomically replaces the value pointed to by `this` with the value of `desired` as if by `p.swap(desired)`. Memory is affected according to the value of `order`.

```cpp
void operator=(shared_ptr<T> desired) noexcept;
```

**5** *Effects:* Equivalent to `store(desired)`.

```cpp
shared_ptr<T> load(memory_order order = memory_order::seq_cst) const noexcept;
```

**6** *Preconditions:* `order` is neither `memory_order::release` nor `memory_order::acq_rel`.

**7** *Effects:* Memory is affected according to the value of `order`.

**8** *Returns:* Atomically returns `p`.

```cpp
operator shared_ptr<T>() const noexcept;
```

**9** *Effects:* Equivalent to: `return load();`

```cpp
shared_ptr<T> exchange(shared_ptr<T> desired, memory_order order = memory_order::seq_cst) noexcept;
```

**10** *Effects:* Atomically replaces `p` with `desired` as if by `p.swap(desired)`. Memory is affected according to the value of `order`. This is an atomic read-modify-write operation ([[intro.races]](../intro.races.md)).

**11** *Returns:* Atomically returns the value of `p` immediately before the effects.

```cpp
bool compare_exchange_weak(shared_ptr<T>& expected, shared_ptr<T> desired,
                           memory_order success, memory_order failure) noexcept;
bool compare_exchange_strong(shared_ptr<T>& expected, shared_ptr<T> desired,
                             memory_order success, memory_order failure) noexcept;
```

**12** *Preconditions:* `failure` is neither `memory_order::release` nor `memory_order::acq_rel`.

**13** *Effects:* If `p` is equivalent to `expected`, assigns `desired` to `p` and has synchronization semantics corresponding to the value of `success`, otherwise assigns `p` to `expected` and has synchronization semantics corresponding to the value of `failure`.

**14** *Returns:* `true` if `p` was equivalent to `expected`, `false` otherwise.

**15** *Remarks:* Two `shared_ptr` objects are equivalent if they store the same pointer value and either share ownership or are both empty. The weak form may fail spuriously. See [[atomics.types.operations]](atomics.types.operations.md).

**16** If the operation returns `true`, `expected` is not accessed after the atomic update and the operation is an atomic read-modify-write operation ([[intro.multithread]](../intro.multithread.md)) on the memory pointed to by `this`. Otherwise, the operation is an atomic load operation on that memory, and `expected` is updated with the existing value read from the atomic object in the attempted atomic update. The `use_count` update corresponding to the write to `expected` is part of the atomic operation. The write to `expected` itself is not required to be part of the atomic operation.

```cpp
bool compare_exchange_weak(shared_ptr<T>& expected, shared_ptr<T> desired,
                           memory_order order = memory_order::seq_cst) noexcept;
```

**17** *Effects:* Equivalent to:

```cpp
return compare_exchange_weak(expected, desired, order, fail_order);
```

where `fail_order` is the same as `order` except that a value of `memory_order::acq_rel` shall be replaced by the value `memory_order::acquire` and a value of `memory_order::release` shall be replaced by the value `memory_order::relaxed`.

```cpp
bool compare_exchange_strong(shared_ptr<T>& expected, shared_ptr<T> desired,
                             memory_order order = memory_order::seq_cst) noexcept;
```

**18** *Effects:* Equivalent to:

```cpp
return compare_exchange_strong(expected, desired, order, fail_order);
```

where `fail_order` is the same as `order` except that a value of `memory_order::acq_rel` shall be replaced by the value `memory_order::acquire` and a value of `memory_order::release` shall be replaced by the value `memory_order::relaxed`.

```cpp
void wait(shared_ptr<T> old, memory_order order = memory_order::seq_cst) const noexcept;
```

**19** *Preconditions:* `order` is neither `memory_order::release` nor `memory_order::acq_rel`.

**20** *Effects:* Repeatedly performs the following steps, in order:

- Evaluates `load(order)` and compares it to `old`.
- If the two are not equivalent, returns.
- Blocks until it is unblocked by an atomic notifying operation or is unblocked spuriously.

**21** *Remarks:* Two `shared_ptr` objects are equivalent if they store the same pointer and either share ownership or are both empty. This function is an atomic waiting operation ([[atomics.wait]](atomics.wait.md)).

```cpp
void notify_one() noexcept;
```

**22** *Effects:* Unblocks the execution of at least one atomic waiting operation that is eligible to be unblocked ([[atomics.wait]](atomics.wait.md)) by this call, if any such atomic waiting operations exist.

**23** *Remarks:* This function is an atomic notifying operation ([[atomics.wait]](atomics.wait.md)).

```cpp
void notify_all() noexcept;
```

**24** *Effects:* Unblocks the execution of all atomic waiting operations that are eligible to be unblocked ([[atomics.wait]](atomics.wait.md)) by this call.

**25** *Remarks:* This function is an atomic notifying operation ([[atomics.wait]](atomics.wait.md)).
