# 31.7.3 Specializations for floating-point types [atomics.ref.float]

[Contents](../README.md) · up: [31.7 Class template `atomic_ref` \[atomics.ref.generic\]](atomics.ref.generic.md) · prev: [31.7.2 Specializations for integral types \[atomics.ref.int\]](atomics.ref.int.md) · next: [31.7.4 Partial specialization for pointers \[atomics.ref.pointer\]](atomics.ref.pointer.md)

**1** There are specializations of the `atomic_ref` class template for the floating-point types `float`, `double`, and `long double`. For each such type `floating-point`, the specialization `atomic_ref<floating-point>` provides additional atomic operations appropriate to floating-point types.

```cpp
namespace std {
  template<> struct atomic_ref<floating-point> {
  private:
    floating-point* ptr;  // exposition only
  public:
    using value_type = floating-point;
    using difference_type = value_type;
    static constexpr size_t required_alignment = implementation-defined;

    static constexpr bool is_always_lock_free = implementation-defined;
    bool is_lock_free() const noexcept;

    explicit atomic_ref(floating-point&);
    atomic_ref(const atomic_ref&) noexcept;
    atomic_ref& operator=(const atomic_ref&) = delete;

    void store(floating-point, memory_order = memory_order::seq_cst) const noexcept;
    floating-point operator=(floating-point) const noexcept;
    floating-point load(memory_order = memory_order::seq_cst) const noexcept;
    operator floating-point() const noexcept;

    floating-point exchange(floating-point,
                            memory_order = memory_order::seq_cst) const noexcept;
    bool compare_exchange_weak(floating-point&, floating-point,
                               memory_order, memory_order) const noexcept;
    bool compare_exchange_strong(floating-point&, floating-point,
                                 memory_order, memory_order) const noexcept;
    bool compare_exchange_weak(floating-point&, floating-point,
                               memory_order = memory_order::seq_cst) const noexcept;
    bool compare_exchange_strong(floating-point&, floating-point,
                                 memory_order = memory_order::seq_cst) const noexcept;

    floating-point fetch_add(floating-point,
                             memory_order = memory_order::seq_cst) const noexcept;
    floating-point fetch_sub(floating-point,
                             memory_order = memory_order::seq_cst) const noexcept;

    floating-point operator+=(floating-point) const noexcept;
    floating-point operator-=(floating-point) const noexcept;

    void wait(floating-point, memory_order = memory_order::seq_cst) const noexcept;
    void notify_one() const noexcept;
    void notify_all() const noexcept;
  };
}
```

**2** Descriptions are provided below only for members that differ from the primary template.

**3** The following operations perform arithmetic computations. The key, operator, and computation correspondence are identified in [Table 144](atomics.types.int.md#tab:atomic.types.int.comp).

```cpp
floating-point fetch_key(floating-point operand,
                          memory_order order = memory_order::seq_cst) const noexcept;
```

**4** *Effects:* Atomically replaces the value referenced by `*ptr` with the result of the computation applied to the value referenced by `*ptr` and the given operand. Memory is affected according to the value of `order`. These operations are atomic read-modify-write operations ([[intro.races]](../intro.races.md)).

**5** *Returns:* Atomically, the value referenced by `*ptr` immediately before the effects.

**6** *Remarks:* If the result is not a representable value for its type ([[expr.pre]](../expr.pre.md)), the result is unspecified, but the operations otherwise have no undefined behavior. Atomic arithmetic operations on `floating-point` should conform to the `std::numeric_limits<floating-point>` traits associated with the floating-point type ([[limits.syn]](limits.syn.md)). The floating-point environment ([[cfenv]](cfenv.md)) for atomic arithmetic operations on `floating-point` may be different than the calling thread’s floating-point environment.

```cpp
floating-point operator op=(floating-point operand) const noexcept;
```

*Effects:* Equivalent to: `return fetch_key(operand) op operand;`
