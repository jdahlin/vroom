# 31.8.3 Specializations for floating-point types [atomics.types.float]

[Contents](../../README.md) · up: [31.8 Class template `atomic` \[atomics.types.generic\]](atomics.types.generic.md) · prev: [31.8.2 Specializations for integers \[atomics.types.int\]](atomics.types.int.md) · next: [31.8.4 Partial specialization for pointers \[atomics.types.pointer\]](atomics.types.pointer.md)

**1** There are specializations of the `atomic` class template for the floating-point types `float`, `double`, and `long double`. For each such type `floating-point`, the specialization `atomic<floating-point>` provides additional atomic operations appropriate to floating-point types.

```cpp
namespace std {
  template<> struct atomic<floating-point> {
    using value_type = floating-point;
    using difference_type = value_type;

    static constexpr bool is_always_lock_free = implementation-defined;
    bool is_lock_free() const volatile noexcept;
    bool is_lock_free() const noexcept;

    constexpr atomic() noexcept;
    constexpr atomic(floating-point) noexcept;
    atomic(const atomic&) = delete;
    atomic& operator=(const atomic&) = delete;
    atomic& operator=(const atomic&) volatile = delete;

    void store(floating-point, memory_order = memory_order::seq_cst) volatile noexcept;
    void store(floating-point, memory_order = memory_order::seq_cst) noexcept;
    floating-point operator=(floating-point) volatile noexcept;
    floating-point operator=(floating-point) noexcept;
    floating-point load(memory_order = memory_order::seq_cst) volatile noexcept;
    floating-point load(memory_order = memory_order::seq_cst) noexcept;
    operator floating-point() volatile noexcept;
    operator floating-point() noexcept;

    floating-point exchange(floating-point,
                            memory_order = memory_order::seq_cst) volatile noexcept;
    floating-point exchange(floating-point,
                            memory_order = memory_order::seq_cst) noexcept;
    bool compare_exchange_weak(floating-point&, floating-point,
                               memory_order, memory_order) volatile noexcept;
    bool compare_exchange_weak(floating-point&, floating-point,
                               memory_order, memory_order) noexcept;
    bool compare_exchange_strong(floating-point&, floating-point,
                                 memory_order, memory_order) volatile noexcept;
    bool compare_exchange_strong(floating-point&, floating-point,
                                 memory_order, memory_order) noexcept;
    bool compare_exchange_weak(floating-point&, floating-point,
                               memory_order = memory_order::seq_cst) volatile noexcept;
    bool compare_exchange_weak(floating-point&, floating-point,
                               memory_order = memory_order::seq_cst) noexcept;
    bool compare_exchange_strong(floating-point&, floating-point,
                                 memory_order = memory_order::seq_cst) volatile noexcept;
    bool compare_exchange_strong(floating-point&, floating-point,
                                 memory_order = memory_order::seq_cst) noexcept;

    floating-point fetch_add(floating-point,
                             memory_order = memory_order::seq_cst) volatile noexcept;
    floating-point fetch_add(floating-point,
                             memory_order = memory_order::seq_cst) noexcept;
    floating-point fetch_sub(floating-point,
                             memory_order = memory_order::seq_cst) volatile noexcept;
    floating-point fetch_sub(floating-point,
                             memory_order = memory_order::seq_cst) noexcept;

    floating-point operator+=(floating-point) volatile noexcept;
    floating-point operator+=(floating-point) noexcept;
    floating-point operator-=(floating-point) volatile noexcept;
    floating-point operator-=(floating-point) noexcept;

    void wait(floating-point, memory_order = memory_order::seq_cst) const volatile noexcept;
    void wait(floating-point, memory_order = memory_order::seq_cst) const noexcept;
    void notify_one() volatile noexcept;
    void notify_one() noexcept;
    void notify_all() volatile noexcept;
    void notify_all() noexcept;
  };
}
```

**2** The atomic floating-point specializations are standard-layout structs. They each have a trivial destructor.

**3** Descriptions are provided below only for members that differ from the primary template.

**4** The following operations perform arithmetic addition and subtraction computations. The key, operator, and computation correspondence are identified in [Table 144](atomics.types.int.md#tab:atomic.types.int.comp).

```cpp
T fetch_key(T operand, memory_order order = memory_order::seq_cst) volatile noexcept;
T fetch_key(T operand, memory_order order = memory_order::seq_cst) noexcept;
```

**5** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**6** *Effects:* Atomically replaces the value pointed to by `this` with the result of the computation applied to the value pointed to by `this` and the given `operand`. Memory is affected according to the value of `order`. These operations are atomic read-modify-write operations ([[intro.multithread]](../../basic/intro.multithread.md)).

**7** *Returns:* Atomically, the value pointed to by `this` immediately before the effects.

**8** *Remarks:* If the result is not a representable value for its type ([[expr.pre]](../../expr/expr.pre.md)) the result is unspecified, but the operations otherwise have no undefined behavior. Atomic arithmetic operations on `floating-point` should conform to the `std::numeric_limits<floating-point>` traits associated with the floating-point type ([[limits.syn]](../support/limits.syn.md)). The floating-point environment ([[cfenv]](../numerics/cfenv.md)) for atomic arithmetic operations on `floating-point` may be different than the calling thread’s floating-point environment.

```cpp
T operator op=(T operand) volatile noexcept;
T operator op=(T operand) noexcept;
```

**9** *Constraints:* For the `volatile` overload of this function, `is_always_lock_free` is `true`.

**10** *Effects:* Equivalent to: `return fetch_key(operand) op operand;`

**11** *Remarks:* If the result is not a representable value for its type ([[expr.pre]](../../expr/expr.pre.md)) the result is unspecified, but the operations otherwise have no undefined behavior. Atomic arithmetic operations on `floating-point` should conform to the `std::numeric_limits<floating-point>` traits associated with the floating-point type ([[limits.syn]](../support/limits.syn.md)). The floating-point environment ([[cfenv]](../numerics/cfenv.md)) for atomic arithmetic operations on `floating-point` may be different than the calling thread’s floating-point environment.
