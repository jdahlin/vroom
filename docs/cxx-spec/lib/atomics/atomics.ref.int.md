# 31.7.2 Specializations for integral types [atomics.ref.int]

[Contents](../../README.md) · up: [31.7 Class template `atomic_ref` \[atomics.ref.generic\]](atomics.ref.generic.md) · prev: [31.7.1 Operations \[atomics.ref.ops\]](atomics.ref.ops.md) · next: [31.7.3 Specializations for floating-point types \[atomics.ref.float\]](atomics.ref.float.md)

**1** There are specializations of the `atomic_ref` class template for the integral types `char`, `signed char`, `unsigned char`, `short`, `unsigned short`, `int`, `unsigned int`, `long`, `unsigned long`, `long long`, `unsigned long long`, `char8_t`, `char16_t`, `char32_t`, `wchar_t`, and any other types needed by the typedefs in the header `<cstdint>` ([[cstdint.syn]](../support/cstdint.syn.md)). For each such type `integral`, the specialization `atomic_ref<integral>` provides additional atomic operations appropriate to integral types. [*Note 1*: The specialization `atomic_ref<bool>` uses the primary template ([[atomics.ref.generic]](atomics.ref.generic.md)). — *end note*]

```cpp
namespace std {
  template<> struct atomic_ref<integral> {
  private:
    integral* ptr;        // exposition only
  public:
    using value_type = integral;
    using difference_type = value_type;
    static constexpr size_t required_alignment = implementation-defined;

    static constexpr bool is_always_lock_free = implementation-defined;
    bool is_lock_free() const noexcept;

    explicit atomic_ref(integral&);
    atomic_ref(const atomic_ref&) noexcept;
    atomic_ref& operator=(const atomic_ref&) = delete;

    void store(integral, memory_order = memory_order::seq_cst) const noexcept;
    integral operator=(integral) const noexcept;
    integral load(memory_order = memory_order::seq_cst) const noexcept;
    operator integral() const noexcept;

    integral exchange(integral,
                      memory_order = memory_order::seq_cst) const noexcept;
    bool compare_exchange_weak(integral&, integral,
                               memory_order, memory_order) const noexcept;
    bool compare_exchange_strong(integral&, integral,
                                 memory_order, memory_order) const noexcept;
    bool compare_exchange_weak(integral&, integral,
                               memory_order = memory_order::seq_cst) const noexcept;
    bool compare_exchange_strong(integral&, integral,
                                 memory_order = memory_order::seq_cst) const noexcept;

    integral fetch_add(integral,
                       memory_order = memory_order::seq_cst) const noexcept;
    integral fetch_sub(integral,
                       memory_order = memory_order::seq_cst) const noexcept;
    integral fetch_and(integral,
                       memory_order = memory_order::seq_cst) const noexcept;
    integral fetch_or(integral,
                      memory_order = memory_order::seq_cst) const noexcept;
    integral fetch_xor(integral,
                       memory_order = memory_order::seq_cst) const noexcept;

    integral operator++(int) const noexcept;
    integral operator--(int) const noexcept;
    integral operator++() const noexcept;
    integral operator--() const noexcept;
    integral operator+=(integral) const noexcept;
    integral operator-=(integral) const noexcept;
    integral operator&=(integral) const noexcept;
    integral operator|=(integral) const noexcept;
    integral operator^=(integral) const noexcept;

    void wait(integral, memory_order = memory_order::seq_cst) const noexcept;
    void notify_one() const noexcept;
    void notify_all() const noexcept;
  };
}
```

**2** Descriptions are provided below only for members that differ from the primary template.

**3** The following operations perform arithmetic computations. The key, operator, and computation correspondence is identified in [Table 144](atomics.types.int.md#tab:atomic.types.int.comp).

```cpp
integral fetch_key(integral operand, memory_order order = memory_order::seq_cst) const noexcept;
```

**4** *Effects:* Atomically replaces the value referenced by `*ptr` with the result of the computation applied to the value referenced by `*ptr` and the given operand. Memory is affected according to the value of `order`. These operations are atomic read-modify-write operations ([[intro.races]](../../basic/intro.races.md)).

**5** *Returns:* Atomically, the value referenced by `*ptr` immediately before the effects.

**6** *Remarks:* For signed integer types, the result is as if the object value and parameters were converted to their corresponding unsigned types, the computation performed on those types, and the result converted back to the signed type. [*Note 2*: There are no undefined results arising from the computation. — *end note*]

```cpp
integral operator op=(integral operand) const noexcept;
```

**7** *Effects:* Equivalent to: `return fetch_key(operand) op operand;`
