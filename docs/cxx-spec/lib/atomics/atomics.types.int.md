# 31.8.2 Specializations for integers [atomics.types.int]

[Contents](../../README.md) · up: [31.8 Class template `atomic` \[atomics.types.generic\]](atomics.types.generic.md) · prev: [31.8.1 Operations on atomic types \[atomics.types.operations\]](atomics.types.operations.md) · next: [31.8.3 Specializations for floating-point types \[atomics.types.float\]](atomics.types.float.md)

**1** There are specializations of the `atomic` class template for the integral types `char`, `signed char`, `unsigned char`, `short`, `unsigned short`, `int`, `unsigned int`, `long`, `unsigned long`, `long long`, `unsigned long long`, `char8_t`, `char16_t`, `char32_t`, `wchar_t`, and any other types needed by the typedefs in the header `<cstdint>` ([[cstdint.syn]](../support/cstdint.syn.md)). For each such type `integral`, the specialization `atomic<integral>` provides additional atomic operations appropriate to integral types. [*Note 1*: The specialization `atomic<bool>` uses the primary template ([[atomics.types.generic]](atomics.types.generic.md)). — *end note*]

```cpp
namespace std {
  template<> struct atomic<integral> {
    using value_type = integral;
    using difference_type = value_type;

    static constexpr bool is_always_lock_free = implementation-defined;
    bool is_lock_free() const volatile noexcept;
    bool is_lock_free() const noexcept;

    constexpr atomic() noexcept;
    constexpr atomic(integral) noexcept;
    atomic(const atomic&) = delete;
    atomic& operator=(const atomic&) = delete;
    atomic& operator=(const atomic&) volatile = delete;

    void store(integral, memory_order = memory_order::seq_cst) volatile noexcept;
    void store(integral, memory_order = memory_order::seq_cst) noexcept;
    integral operator=(integral) volatile noexcept;
    integral operator=(integral) noexcept;
    integral load(memory_order = memory_order::seq_cst) const volatile noexcept;
    integral load(memory_order = memory_order::seq_cst) const noexcept;
    operator integral() const volatile noexcept;
    operator integral() const noexcept;

    integral exchange(integral, memory_order = memory_order::seq_cst) volatile noexcept;
    integral exchange(integral, memory_order = memory_order::seq_cst) noexcept;
    bool compare_exchange_weak(integral&, integral,
                               memory_order, memory_order) volatile noexcept;
    bool compare_exchange_weak(integral&, integral,
                               memory_order, memory_order) noexcept;
    bool compare_exchange_strong(integral&, integral,
                                 memory_order, memory_order) volatile noexcept;
    bool compare_exchange_strong(integral&, integral,
                                 memory_order, memory_order) noexcept;
    bool compare_exchange_weak(integral&, integral,
                               memory_order = memory_order::seq_cst) volatile noexcept;
    bool compare_exchange_weak(integral&, integral,
                               memory_order = memory_order::seq_cst) noexcept;
    bool compare_exchange_strong(integral&, integral,
                                 memory_order = memory_order::seq_cst) volatile noexcept;
    bool compare_exchange_strong(integral&, integral,
                                 memory_order = memory_order::seq_cst) noexcept;

    integral fetch_add(integral, memory_order = memory_order::seq_cst) volatile noexcept;
    integral fetch_add(integral, memory_order = memory_order::seq_cst) noexcept;
    integral fetch_sub(integral, memory_order = memory_order::seq_cst) volatile noexcept;
    integral fetch_sub(integral, memory_order = memory_order::seq_cst) noexcept;
    integral fetch_and(integral, memory_order = memory_order::seq_cst) volatile noexcept;
    integral fetch_and(integral, memory_order = memory_order::seq_cst) noexcept;
    integral fetch_or(integral, memory_order = memory_order::seq_cst) volatile noexcept;
    integral fetch_or(integral, memory_order = memory_order::seq_cst) noexcept;
    integral fetch_xor(integral, memory_order = memory_order::seq_cst) volatile noexcept;
    integral fetch_xor(integral, memory_order = memory_order::seq_cst) noexcept;

    integral operator++(int) volatile noexcept;
    integral operator++(int) noexcept;
    integral operator--(int) volatile noexcept;
    integral operator--(int) noexcept;
    integral operator++() volatile noexcept;
    integral operator++() noexcept;
    integral operator--() volatile noexcept;
    integral operator--() noexcept;
    integral operator+=(integral) volatile noexcept;
    integral operator+=(integral) noexcept;
    integral operator-=(integral) volatile noexcept;
    integral operator-=(integral) noexcept;
    integral operator&=(integral) volatile noexcept;
    integral operator&=(integral) noexcept;
    integral operator|=(integral) volatile noexcept;
    integral operator|=(integral) noexcept;
    integral operator^=(integral) volatile noexcept;
    integral operator^=(integral) noexcept;

    void wait(integral, memory_order = memory_order::seq_cst) const volatile noexcept;
    void wait(integral, memory_order = memory_order::seq_cst) const noexcept;
    void notify_one() volatile noexcept;
    void notify_one() noexcept;
    void notify_all() volatile noexcept;
    void notify_all() noexcept;
  };
}
```

**2** The atomic integral specializations are standard-layout structs. They each have a trivial destructor.

**3** Descriptions are provided below only for members that differ from the primary template.

**4** The following operations perform arithmetic computations. The key, operator, and computation correspondence is:

<a id="tab:atomic.types.int.comp"></a>
**Table 144: Atomic arithmetic computations** [tab:atomic.types.int.comp]

| `key` | Op | Computation | `key` | Op | Computation |
| --- | --- | --- | --- | --- | --- |
| `add` | `+` | addition | `sub` | `-` | subtraction |
| `or` | `\|` | bitwise inclusive or | `xor` | `^` | bitwise exclusive or |
| `and` | `&` | bitwise and |  |  |  |

```cpp
T fetch_key(T operand, memory_order order = memory_order::seq_cst) volatile noexcept;
T fetch_key(T operand, memory_order order = memory_order::seq_cst) noexcept;
```

**5** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**6** *Effects:* Atomically replaces the value pointed to by `this` with the result of the computation applied to the value pointed to by `this` and the given `operand`. Memory is affected according to the value of `order`. These operations are atomic read-modify-write operations ([[intro.multithread]](../../basic/intro.multithread.md)).

**7** *Returns:* Atomically, the value pointed to by `this` immediately before the effects.

**8** *Remarks:* For signed integer types, the result is as if the object value and parameters were converted to their corresponding unsigned types, the computation performed on those types, and the result converted back to the signed type. [*Note 2*: There are no undefined results arising from the computation. — *end note*]

```cpp
T operator op=(T operand) volatile noexcept;
T operator op=(T operand) noexcept;
```

**9** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**10** *Effects:* Equivalent to: `return fetch_key(operand) op operand;`
