# 31.8.4 Partial specialization for pointers [atomics.types.pointer]

[Contents](../../README.md) · up: [31.8 Class template `atomic` \[atomics.types.generic\]](atomics.types.generic.md) · prev: [31.8.3 Specializations for floating-point types \[atomics.types.float\]](atomics.types.float.md) · next: [31.8.5 Member operators common to integers and pointers to objects \[atomics.types.memop\]](atomics.types.memop.md)

```cpp
namespace std {
  template<class T> struct atomic<T*> {
    using value_type = T*;
    using difference_type = ptrdiff_t;

    static constexpr bool is_always_lock_free = implementation-defined;
    bool is_lock_free() const volatile noexcept;
    bool is_lock_free() const noexcept;

    constexpr atomic() noexcept;
    constexpr atomic(T*) noexcept;
    atomic(const atomic&) = delete;
    atomic& operator=(const atomic&) = delete;
    atomic& operator=(const atomic&) volatile = delete;

    void store(T*, memory_order = memory_order::seq_cst) volatile noexcept;
    void store(T*, memory_order = memory_order::seq_cst) noexcept;
    T* operator=(T*) volatile noexcept;
    T* operator=(T*) noexcept;
    T* load(memory_order = memory_order::seq_cst) const volatile noexcept;
    T* load(memory_order = memory_order::seq_cst) const noexcept;
    operator T*() const volatile noexcept;
    operator T*() const noexcept;

    T* exchange(T*, memory_order = memory_order::seq_cst) volatile noexcept;
    T* exchange(T*, memory_order = memory_order::seq_cst) noexcept;
    bool compare_exchange_weak(T*&, T*, memory_order, memory_order) volatile noexcept;
    bool compare_exchange_weak(T*&, T*, memory_order, memory_order) noexcept;
    bool compare_exchange_strong(T*&, T*, memory_order, memory_order) volatile noexcept;
    bool compare_exchange_strong(T*&, T*, memory_order, memory_order) noexcept;
    bool compare_exchange_weak(T*&, T*,
                               memory_order = memory_order::seq_cst) volatile noexcept;
    bool compare_exchange_weak(T*&, T*,
                               memory_order = memory_order::seq_cst) noexcept;
    bool compare_exchange_strong(T*&, T*,
                                 memory_order = memory_order::seq_cst) volatile noexcept;
    bool compare_exchange_strong(T*&, T*,
                                 memory_order = memory_order::seq_cst) noexcept;

    T* fetch_add(ptrdiff_t, memory_order = memory_order::seq_cst) volatile noexcept;
    T* fetch_add(ptrdiff_t, memory_order = memory_order::seq_cst) noexcept;
    T* fetch_sub(ptrdiff_t, memory_order = memory_order::seq_cst) volatile noexcept;
    T* fetch_sub(ptrdiff_t, memory_order = memory_order::seq_cst) noexcept;

    T* operator++(int) volatile noexcept;
    T* operator++(int) noexcept;
    T* operator--(int) volatile noexcept;
    T* operator--(int) noexcept;
    T* operator++() volatile noexcept;
    T* operator++() noexcept;
    T* operator--() volatile noexcept;
    T* operator--() noexcept;
    T* operator+=(ptrdiff_t) volatile noexcept;
    T* operator+=(ptrdiff_t) noexcept;
    T* operator-=(ptrdiff_t) volatile noexcept;
    T* operator-=(ptrdiff_t) noexcept;

    void wait(T*, memory_order = memory_order::seq_cst) const volatile noexcept;
    void wait(T*, memory_order = memory_order::seq_cst) const noexcept;
    void notify_one() volatile noexcept;
    void notify_one() noexcept;
    void notify_all() volatile noexcept;
    void notify_all() noexcept;
  };
}
```

**1** There is a partial specialization of the `atomic` class template for pointers. Specializations of this partial specialization are standard-layout structs. They each have a trivial destructor.

**2** Descriptions are provided below only for members that differ from the primary template.

**3** The following operations perform pointer arithmetic. The key, operator, and computation correspondence is:

<a id="tab:atomic.types.pointer.comp"></a>
**Table 145: Atomic pointer computations** [tab:atomic.types.pointer.comp]

|  |  |  |  |  |  |
| --- | --- | --- | --- | --- | --- |
| `Key` | Op | Computation | `Key` | Op | Computation |
| `add` | `+` | addition | `sub` | `-` | subtraction |

```cpp
T* fetch_key(ptrdiff_t operand, memory_order order = memory_order::seq_cst) volatile noexcept;
T* fetch_key(ptrdiff_t operand, memory_order order = memory_order::seq_cst) noexcept;
```

**4** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**5** *Mandates:* `T` is a complete object type. [*Note 1*: Pointer arithmetic on `void*` or function pointers is ill-formed. — *end note*]

**6** *Effects:* Atomically replaces the value pointed to by `this` with the result of the computation applied to the value pointed to by `this` and the given `operand`. Memory is affected according to the value of `order`. These operations are atomic read-modify-write operations ([[intro.multithread]](../../basic/intro.multithread.md)).

**7** *Returns:* Atomically, the value pointed to by `this` immediately before the effects.

**8** *Remarks:* The result may be an undefined address, but the operations otherwise have no undefined behavior.

```cpp
T* operator op=(ptrdiff_t operand) volatile noexcept;
T* operator op=(ptrdiff_t operand) noexcept;
```

**9** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**10** *Effects:* Equivalent to: `return fetch_key(operand) op operand;`
