# 31.8.1 Operations on atomic types [atomics.types.operations]

[Contents](../README.md) · up: [31.8 Class template `atomic` \[atomics.types.generic\]](atomics.types.generic.md) · prev: [31.8 Class template `atomic` \[atomics.types.generic\]](atomics.types.generic.md) · next: [31.8.2 Specializations for integers \[atomics.types.int\]](atomics.types.int.md)

```cpp
constexpr atomic() noexcept(is_nothrow_default_constructible_v<T>);
```

**1** *Mandates:* `is_default_constructible_v<T>` is `true`.

**2** *Effects:* Initializes the atomic object with the value of `T()`. Initialization is not an atomic operation ([[intro.multithread]](../intro.multithread.md)).

```cpp
constexpr atomic(T desired) noexcept;
```

**3** *Effects:* Initializes the object with the value `desired`. Initialization is not an atomic operation ([[intro.multithread]](../intro.multithread.md)). [*Note 1*: It is possible to have an access to an atomic object `A` race with its construction, for example by communicating the address of the just-constructed object `A` to another thread via `memory_order::relaxed` operations on a suitable atomic pointer variable, and then immediately accessing `A` in the receiving thread. This results in undefined behavior. — *end note*]

```cpp
static constexpr bool is_always_lock_free = implementation-defined;
```

**4** The `static` data member `is_always_lock_free` is `true` if the atomic type’s operations are always lock-free, and `false` otherwise. [*Note 2*: The value of `is_always_lock_free` is consistent with the value of the corresponding `ATOMIC_..._LOCK_FREE` macro, if defined. — *end note*]

```cpp
bool is_lock_free() const volatile noexcept;
bool is_lock_free() const noexcept;
```

**5** *Returns:* `true` if the object’s operations are lock-free, `false` otherwise. [*Note 3*: The return value of the `is_lock_free` member function is consistent with the value of `is_always_lock_free` for the same type. — *end note*]

```cpp
void store(T desired, memory_order order = memory_order::seq_cst) volatile noexcept;
void store(T desired, memory_order order = memory_order::seq_cst) noexcept;
```

**6** *Preconditions:* The `order` argument is neither `memory_order::consume`, `memory_order::acquire`, nor `memory_order::acq_rel`.

**7** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**8** *Effects:* Atomically replaces the value pointed to by `this` with the value of `desired`. Memory is affected according to the value of `order`.

```cpp
T operator=(T desired) volatile noexcept;
T operator=(T desired) noexcept;
```

**9** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**10** *Effects:* Equivalent to `store(desired)`.

**11** *Returns:* `desired`.

```cpp
T load(memory_order order = memory_order::seq_cst) const volatile noexcept;
T load(memory_order order = memory_order::seq_cst) const noexcept;
```

**12** *Preconditions:* The `order` argument is neither `memory_order::release` nor `memory_order::acq_rel`.

**13** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**14** *Effects:* Memory is affected according to the value of `order`.

**15** *Returns:* Atomically returns the value pointed to by `this`.

```cpp
operator T() const volatile noexcept;
operator T() const noexcept;
```

**16** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**17** *Effects:* Equivalent to: `return load();`

```cpp
T exchange(T desired, memory_order order = memory_order::seq_cst) volatile noexcept;
T exchange(T desired, memory_order order = memory_order::seq_cst) noexcept;
```

**18** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**19** *Effects:* Atomically replaces the value pointed to by `this` with `desired`. Memory is affected according to the value of `order`. These operations are atomic read-modify-write operations ([[intro.multithread]](../intro.multithread.md)).

**20** *Returns:* Atomically returns the value pointed to by `this` immediately before the effects.

```cpp
bool compare_exchange_weak(T& expected, T desired,
                           memory_order success, memory_order failure) volatile noexcept;
bool compare_exchange_weak(T& expected, T desired,
                           memory_order success, memory_order failure) noexcept;
bool compare_exchange_strong(T& expected, T desired,
                             memory_order success, memory_order failure) volatile noexcept;
bool compare_exchange_strong(T& expected, T desired,
                             memory_order success, memory_order failure) noexcept;
bool compare_exchange_weak(T& expected, T desired,
                           memory_order order = memory_order::seq_cst) volatile noexcept;
bool compare_exchange_weak(T& expected, T desired,
                           memory_order order = memory_order::seq_cst) noexcept;
bool compare_exchange_strong(T& expected, T desired,
                             memory_order order = memory_order::seq_cst) volatile noexcept;
bool compare_exchange_strong(T& expected, T desired,
                             memory_order order = memory_order::seq_cst) noexcept;
```

**21** *Preconditions:* The `failure` argument is neither `memory_order::release` nor `memory_order::acq_rel`.

**22** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**23** *Effects:* Retrieves the value in `expected`. It then atomically compares the value representation of the value pointed to by `this` for equality with that previously retrieved from `expected`, and if true, replaces the value pointed to by `this` with that in `desired`. If and only if the comparison is `true`, memory is affected according to the value of `success`, and if the comparison is false, memory is affected according to the value of `failure`. When only one `memory_order` argument is supplied, the value of `success` is `order`, and the value of `failure` is `order` except that a value of `memory_order::acq_rel` shall be replaced by the value `memory_order::acquire` and a value of `memory_order::release` shall be replaced by the value `memory_order::relaxed`. If and only if the comparison is false then, after the atomic operation, the value in `expected` is replaced by the value pointed to by `this` during the atomic comparison. If the operation returns `true`, these operations are atomic read-modify-write operations ([[intro.multithread]](../intro.multithread.md)) on the memory pointed to by `this`. Otherwise, these operations are atomic load operations on that memory.

**24** *Returns:* The result of the comparison.

**25** [*Note 4*: For example, the effect of `compare_exchange_strong` on objects without padding bits ([[basic.types]](../basic.types.md)) is

```cpp
if (memcmp(this, &expected, sizeof(*this)) == 0)
  memcpy(this, &desired, sizeof(*this));
else
  memcpy(expected, this, sizeof(*this));
```

— *end note*] [*Example 1*: The expected use of the compare-and-exchange operations is as follows. The compare-and-exchange operations will update `expected` when another iteration of the loop is needed.

```cpp
expected = current.load();
do {
  desired = function(expected);
} while (!current.compare_exchange_weak(expected, desired));
```

— *end example*] [*Example 2*: Because the expected value is updated only on failure, code releasing the memory containing the `expected` value on success will work. For example, list head insertion will act atomically and would not introduce a data race in the following code:

```cpp
do {
  p->next = head;                                   // make new list node point to the current head
} while (!head.compare_exchange_weak(p->next, p));  // try to insert
```

— *end example*]

**26** Implementations should ensure that weak compare-and-exchange operations do not consistently return `false` unless either the atomic object has value different from `expected` or there are concurrent modifications to the atomic object.

**27** *Remarks:* A weak compare-and-exchange operation may fail spuriously. That is, even when the contents of memory referred to by `expected` and `this` are equal, it may return `false` and store back to `expected` the same memory contents that were originally there. [*Note 5*: This spurious failure enables implementation of compare-and-exchange on a broader class of machines, e.g., load-locked store-conditional machines. A consequence of spurious failure is that nearly all uses of weak compare-and-exchange will be in a loop. When a compare-and-exchange is in a loop, the weak version will yield better performance on some platforms. When a weak compare-and-exchange would require a loop and a strong one would not, the strong one is preferable. — *end note*]

**28** [*Note 6*: Under cases where the `memcpy` and `memcmp` semantics of the compare-and-exchange operations apply, the outcome might be failed comparisons for values that compare equal with `operator==` if the value representation has trap bits or alternate representations of the same value. Notably, on implementations conforming to ISO/IEC/IEEE 60559, floating-point `-0.0` and `+0.0` will not compare equal with `memcmp` but will compare equal with `operator==`, and NaNs with the same payload will compare equal with `memcmp` but will not compare equal with `operator==`. — *end note*] [*Note 7*: Because compare-and-exchange acts on an object’s value representation, padding bits that never participate in the object’s value representation are ignored. As a consequence, the following code is guaranteed to avoid spurious failure:

```cpp
struct padded {
  char clank = 0x42;
  // Padding here.
  unsigned biff = 0xC0DEFEFE;
};
atomic<padded> pad = {};

bool zap() {
  padded expected, desired{0, 0};
  return pad.compare_exchange_strong(expected, desired);
}
```

— *end note*] [*Note 8*: For a union with bits that participate in the value representation of some members but not others, compare-and-exchange might always fail. This is because such padding bits have an indeterminate value when they do not participate in the value representation of the active member. As a consequence, the following code is not guaranteed to ever succeed:

```cpp
union pony {
  double celestia = 0.;
  short luna;       // padded
};
atomic<pony> princesses = {};

bool party(pony desired) {
  pony expected;
  return princesses.compare_exchange_strong(expected, desired);
}
```

— *end note*]

```cpp
void wait(T old, memory_order order = memory_order::seq_cst) const volatile noexcept;
void wait(T old, memory_order order = memory_order::seq_cst) const noexcept;
```

**29** *Preconditions:* `order` is neither `memory_order::release` nor `memory_order::acq_rel`.

**30** *Effects:* Repeatedly performs the following steps, in order:

- Evaluates `load(order)` and compares its value representation for equality against that of `old`.
- If they compare unequal, returns.
- Blocks until it is unblocked by an atomic notifying operation or is unblocked spuriously.

**31** *Remarks:* This function is an atomic waiting operation ([[atomics.wait]](atomics.wait.md)).

```cpp
void notify_one() volatile noexcept;
void notify_one() noexcept;
```

**32** *Effects:* Unblocks the execution of at least one atomic waiting operation that is eligible to be unblocked ([[atomics.wait]](atomics.wait.md)) by this call, if any such atomic waiting operations exist.

**33** *Remarks:* This function is an atomic notifying operation ([[atomics.wait]](atomics.wait.md)).

```cpp
void notify_all() volatile noexcept;
void notify_all() noexcept;
```

**34** *Effects:* Unblocks the execution of all atomic waiting operations that are eligible to be unblocked ([[atomics.wait]](atomics.wait.md)) by this call.

**35** *Remarks:* This function is an atomic notifying operation ([[atomics.wait]](atomics.wait.md)).
