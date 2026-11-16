# 31.7.1 Operations [atomics.ref.ops]

[Contents](../../README.md) · up: [31.7 Class template `atomic_ref` \[atomics.ref.generic\]](atomics.ref.generic.md) · prev: [31.7 Class template `atomic_ref` \[atomics.ref.generic\]](atomics.ref.generic.md) · next: [31.7.2 Specializations for integral types \[atomics.ref.int\]](atomics.ref.int.md)

```cpp
static constexpr size_t required_alignment;
```

**1** The alignment required for an object to be referenced by an atomic reference, which is at least `alignof(T)`.

**2** [*Note 1*: Hardware could require an object referenced by an `atomic_ref` to have stricter alignment ([[basic.align]](../../basic/basic.align.md)) than other objects of type `T`. Further, whether operations on an `atomic_ref` are lock-free could depend on the alignment of the referenced object. For example, lock-free operations on `std::complex<double>` could be supported only if aligned to `2*alignof(double)`. — *end note*]

```cpp
static constexpr bool is_always_lock_free;
```

**3** The static data member `is_always_lock_free` is `true` if the `atomic_ref` type’s operations are always lock-free, and `false` otherwise.

```cpp
bool is_lock_free() const noexcept;
```

**4** *Returns:* `true` if operations on all objects of the type `atomic_ref<T>` are lock-free, `false` otherwise.

```cpp
atomic_ref(T& obj);
```

**5** *Preconditions:* The referenced object is aligned to `required_alignment`.

**6** *Postconditions:* `*this` references `obj`.

**7** *Throws:* Nothing.

```cpp
atomic_ref(const atomic_ref& ref) noexcept;
```

**8** *Postconditions:* `*this` references the object referenced by `ref`.

```cpp
void store(T desired, memory_order order = memory_order::seq_cst) const noexcept;
```

**9** *Preconditions:* The `order` argument is neither `memory_order::consume`, `memory_order::acquire`, nor `memory_order::acq_rel`.

**10** *Effects:* Atomically replaces the value referenced by `*ptr` with the value of `desired`. Memory is affected according to the value of `order`.

```cpp
T operator=(T desired) const noexcept;
```

**11** *Effects:* Equivalent to:

```cpp
store(desired);
return desired;
```

```cpp
T load(memory_order order = memory_order::seq_cst) const noexcept;
```

**12** *Preconditions:* The `order` argument is neither `memory_order::release` nor `memory_order::acq_rel`.

**13** *Effects:* Memory is affected according to the value of `order`.

**14** *Returns:* Atomically returns the value referenced by `*ptr`.

```cpp
operator T() const noexcept;
```

**15** *Effects:* Equivalent to: `return load();`

```cpp
T exchange(T desired, memory_order order = memory_order::seq_cst) const noexcept;
```

**16** *Effects:* Atomically replaces the value referenced by `*ptr` with `desired`. Memory is affected according to the value of `order`. This operation is an atomic read-modify-write operation ([[intro.multithread]](../../basic/intro.multithread.md)).

**17** *Returns:* Atomically returns the value referenced by `*ptr` immediately before the effects.

```cpp
bool compare_exchange_weak(T& expected, T desired,
                           memory_order success, memory_order failure) const noexcept;

bool compare_exchange_strong(T& expected, T desired,
                             memory_order success, memory_order failure) const noexcept;

bool compare_exchange_weak(T& expected, T desired,
                           memory_order order = memory_order::seq_cst) const noexcept;

bool compare_exchange_strong(T& expected, T desired,
                             memory_order order = memory_order::seq_cst) const noexcept;
```

**18** *Preconditions:* The `failure` argument is neither `memory_order::release` nor `memory_order::acq_rel`.

**19** *Effects:* Retrieves the value in `expected`. It then atomically compares the value representation of the value referenced by `*ptr` for equality with that previously retrieved from `expected`, and if `true`, replaces the value referenced by `*ptr` with that in `desired`. If and only if the comparison is `true`, memory is affected according to the value of `success`, and if the comparison is `false`, memory is affected according to the value of `failure`. When only one `memory_order` argument is supplied, the value of `success` is `order`, and the value of `failure` is `order` except that a value of `memory_order::acq_rel` shall be replaced by the value `memory_order::acquire` and a value of `memory_order::release` shall be replaced by the value `memory_order::relaxed`. If and only if the comparison is `false` then, after the atomic operation, the value in `expected` is replaced by the value read from the value referenced by `*ptr` during the atomic comparison. If the operation returns `true`, these operations are atomic read-modify-write operations ([[intro.races]](../../basic/intro.races.md)) on the value referenced by `*ptr`. Otherwise, these operations are atomic load operations on that memory.

**20** *Returns:* The result of the comparison.

**21** *Remarks:* A weak compare-and-exchange operation may fail spuriously. That is, even when the contents of memory referred to by `expected` and `ptr` are equal, it may return `false` and store back to `expected` the same memory contents that were originally there. [*Note 2*: This spurious failure enables implementation of compare-and-exchange on a broader class of machines, e.g., load-locked store-conditional machines. A consequence of spurious failure is that nearly all uses of weak compare-and-exchange will be in a loop. When a compare-and-exchange is in a loop, the weak version will yield better performance on some platforms. When a weak compare-and-exchange would require a loop and a strong one would not, the strong one is preferable. — *end note*]

```cpp
void wait(T old, memory_order order = memory_order::seq_cst) const noexcept;
```

**22** *Preconditions:* `order` is neither `memory_order::release` nor `memory_order::acq_rel`.

**23** *Effects:* Repeatedly performs the following steps, in order:

- Evaluates `load(order)` and compares its value representation for equality against that of `old`.
- If they compare unequal, returns.
- Blocks until it is unblocked by an atomic notifying operation or is unblocked spuriously.

**24** *Remarks:* This function is an atomic waiting operation ([[atomics.wait]](atomics.wait.md)) on atomic object `*ptr`.

```cpp
void notify_one() const noexcept;
```

**25** *Effects:* Unblocks the execution of at least one atomic waiting operation on `*ptr` that is eligible to be unblocked ([[atomics.wait]](atomics.wait.md)) by this call, if any such atomic waiting operations exist.

**26** *Remarks:* This function is an atomic notifying operation ([[atomics.wait]](atomics.wait.md)) on atomic object `*ptr`.

```cpp
void notify_all() const noexcept;
```

**27** *Effects:* Unblocks the execution of all atomic waiting operations on `*ptr` that are eligible to be unblocked ([[atomics.wait]](atomics.wait.md)) by this call.

**28** *Remarks:* This function is an atomic notifying operation ([[atomics.wait]](atomics.wait.md)) on atomic object `*ptr`.
