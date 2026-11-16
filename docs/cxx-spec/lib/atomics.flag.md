# 31.10 Flag type and operations [atomics.flag]

[Contents](../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31.9 Non-member functions \[atomics.nonmembers\]](atomics.nonmembers.md) · next: [31.11 Fences \[atomics.fences\]](atomics.fences.md)

```cpp
namespace std {
  struct atomic_flag {
    constexpr atomic_flag() noexcept;
    atomic_flag(const atomic_flag&) = delete;
    atomic_flag& operator=(const atomic_flag&) = delete;
    atomic_flag& operator=(const atomic_flag&) volatile = delete;

    bool test(memory_order = memory_order::seq_cst) const volatile noexcept;
    bool test(memory_order = memory_order::seq_cst) const noexcept;
    bool test_and_set(memory_order = memory_order::seq_cst) volatile noexcept;
    bool test_and_set(memory_order = memory_order::seq_cst) noexcept;
    void clear(memory_order = memory_order::seq_cst) volatile noexcept;
    void clear(memory_order = memory_order::seq_cst) noexcept;

    void wait(bool, memory_order = memory_order::seq_cst) const volatile noexcept;
    void wait(bool, memory_order = memory_order::seq_cst) const noexcept;
    void notify_one() volatile noexcept;
    void notify_one() noexcept;
    void notify_all() volatile noexcept;
    void notify_all() noexcept;
  };
}
```

**1** The `atomic_flag` type provides the classic test-and-set functionality. It has two states, set and clear.

**2** Operations on an object of type `atomic_flag` shall be lock-free. [*Note 1*: Hence the operations should also be address-free. — *end note*]

**3** The `atomic_flag` type is a standard-layout struct. It has a trivial destructor.

```cpp
constexpr atomic_flag::atomic_flag() noexcept;
```

**4** *Effects:* Initializes `*this` to the clear state.

```cpp
bool atomic_flag_test(const volatile atomic_flag* object) noexcept;
bool atomic_flag_test(const atomic_flag* object) noexcept;
bool atomic_flag_test_explicit(const volatile atomic_flag* object,
                               memory_order order) noexcept;
bool atomic_flag_test_explicit(const atomic_flag* object,
                               memory_order order) noexcept;
bool atomic_flag::test(memory_order order = memory_order::seq_cst) const volatile noexcept;
bool atomic_flag::test(memory_order order = memory_order::seq_cst) const noexcept;
```

**5** For `atomic_flag_test`, let `order` be `memory_order::seq_cst`.

**6** *Preconditions:* `order` is neither `memory_order::release` nor `memory_order::acq_rel`.

**7** *Effects:* Memory is affected according to the value of `order`.

**8** *Returns:* Atomically returns the value pointed to by `object` or `this`.

```cpp
bool atomic_flag_test_and_set(volatile atomic_flag* object) noexcept;
bool atomic_flag_test_and_set(atomic_flag* object) noexcept;
bool atomic_flag_test_and_set_explicit(volatile atomic_flag* object, memory_order order) noexcept;
bool atomic_flag_test_and_set_explicit(atomic_flag* object, memory_order order) noexcept;
bool atomic_flag::test_and_set(memory_order order = memory_order::seq_cst) volatile noexcept;
bool atomic_flag::test_and_set(memory_order order = memory_order::seq_cst) noexcept;
```

**9** *Effects:* Atomically sets the value pointed to by `object` or by `this` to `true`. Memory is affected according to the value of `order`. These operations are atomic read-modify-write operations ([[intro.multithread]](../intro.multithread.md)).

**10** *Returns:* Atomically, the value of the object immediately before the effects.

```cpp
void atomic_flag_clear(volatile atomic_flag* object) noexcept;
void atomic_flag_clear(atomic_flag* object) noexcept;
void atomic_flag_clear_explicit(volatile atomic_flag* object, memory_order order) noexcept;
void atomic_flag_clear_explicit(atomic_flag* object, memory_order order) noexcept;
void atomic_flag::clear(memory_order order = memory_order::seq_cst) volatile noexcept;
void atomic_flag::clear(memory_order order = memory_order::seq_cst) noexcept;
```

**11** *Preconditions:* The `order` argument is neither `memory_order::consume`, `memory_order::acquire`, nor `memory_order::acq_rel`.

**12** *Effects:* Atomically sets the value pointed to by `object` or by `this` to `false`. Memory is affected according to the value of `order`.

```cpp
void atomic_flag_wait(const volatile atomic_flag* object, bool old) noexcept;
void atomic_flag_wait(const atomic_flag* object, bool old) noexcept;
void atomic_flag_wait_explicit(const volatile atomic_flag* object,
                               bool old, memory_order order) noexcept;
void atomic_flag_wait_explicit(const atomic_flag* object,
                               bool old, memory_order order) noexcept;
void atomic_flag::wait(bool old, memory_order order =
                                   memory_order::seq_cst) const volatile noexcept;
void atomic_flag::wait(bool old, memory_order order =
                                   memory_order::seq_cst) const noexcept;
```

**13** For `atomic_flag_wait`, let `order` be `memory_order::seq_cst`. Let `flag` be `object` for the non-member functions and `this` for the member functions.

**14** *Preconditions:* `order` is neither `memory_order::release` nor `memory_order::acq_rel`.

**15** *Effects:* Repeatedly performs the following steps, in order:

- Evaluates `flag->test(order) != old`.
- If the result of that evaluation is `true`, returns.
- Blocks until it is unblocked by an atomic notifying operation or is unblocked spuriously.

**16** *Remarks:* This function is an atomic waiting operation ([[atomics.wait]](atomics.wait.md)).

```cpp
void atomic_flag_notify_one(volatile atomic_flag* object) noexcept;
void atomic_flag_notify_one(atomic_flag* object) noexcept;
void atomic_flag::notify_one() volatile noexcept;
void atomic_flag::notify_one() noexcept;
```

**17** *Effects:* Unblocks the execution of at least one atomic waiting operation that is eligible to be unblocked ([[atomics.wait]](atomics.wait.md)) by this call, if any such atomic waiting operations exist.

**18** *Remarks:* This function is an atomic notifying operation ([[atomics.wait]](atomics.wait.md)).

```cpp
void atomic_flag_notify_all(volatile atomic_flag* object) noexcept;
void atomic_flag_notify_all(atomic_flag* object) noexcept;
void atomic_flag::notify_all() volatile noexcept;
void atomic_flag::notify_all() noexcept;
```

**19** *Effects:* Unblocks the execution of all atomic waiting operations that are eligible to be unblocked ([[atomics.wait]](atomics.wait.md)) by this call.

**20** *Remarks:* This function is an atomic notifying operation ([[atomics.wait]](atomics.wait.md)).
