# 31.7.4 Partial specialization for pointers [atomics.ref.pointer]

[Contents](../../README.md) · up: [31.7 Class template `atomic_ref` \[atomics.ref.generic\]](atomics.ref.generic.md) · prev: [31.7.3 Specializations for floating-point types \[atomics.ref.float\]](atomics.ref.float.md) · next: [31.7.5 Member operators common to integers and pointers to objects \[atomics.ref.memop\]](atomics.ref.memop.md)

```cpp
namespace std {
  template<class T> struct atomic_ref<T*> {
  private:
    T** ptr;        // exposition only
  public:
    using value_type = T*;
    using difference_type = ptrdiff_t;
    static constexpr size_t required_alignment = implementation-defined;

    static constexpr bool is_always_lock_free = implementation-defined;
    bool is_lock_free() const noexcept;

    explicit atomic_ref(T*&);
    atomic_ref(const atomic_ref&) noexcept;
    atomic_ref& operator=(const atomic_ref&) = delete;

    void store(T*, memory_order = memory_order::seq_cst) const noexcept;
    T* operator=(T*) const noexcept;
    T* load(memory_order = memory_order::seq_cst) const noexcept;
    operator T*() const noexcept;

    T* exchange(T*, memory_order = memory_order::seq_cst) const noexcept;
    bool compare_exchange_weak(T*&, T*,
                               memory_order, memory_order) const noexcept;
    bool compare_exchange_strong(T*&, T*,
                                 memory_order, memory_order) const noexcept;
    bool compare_exchange_weak(T*&, T*,
                               memory_order = memory_order::seq_cst) const noexcept;
    bool compare_exchange_strong(T*&, T*,
                                 memory_order = memory_order::seq_cst) const noexcept;

    T* fetch_add(difference_type, memory_order = memory_order::seq_cst) const noexcept;
    T* fetch_sub(difference_type, memory_order = memory_order::seq_cst) const noexcept;

    T* operator++(int) const noexcept;
    T* operator--(int) const noexcept;
    T* operator++() const noexcept;
    T* operator--() const noexcept;
    T* operator+=(difference_type) const noexcept;
    T* operator-=(difference_type) const noexcept;

    void wait(T*, memory_order = memory_order::seq_cst) const noexcept;
    void notify_one() const noexcept;
    void notify_all() const noexcept;
  };
}
```

**1** Descriptions are provided below only for members that differ from the primary template.

**2** The following operations perform arithmetic computations. The key, operator, and computation correspondence is identified in [Table 145](atomics.types.pointer.md#tab:atomic.types.pointer.comp).

```cpp
T* fetch_key(difference_type operand, memory_order order = memory_order::seq_cst) const noexcept;
```

**3** *Mandates:* `T` is a complete object type.

**4** *Effects:* Atomically replaces the value referenced by `*ptr` with the result of the computation applied to the value referenced by `*ptr` and the given operand. Memory is affected according to the value of `order`. These operations are atomic read-modify-write operations ([[intro.races]](../../basic/intro.races.md)).

**5** *Returns:* Atomically, the value referenced by `*ptr` immediately before the effects.

**6** *Remarks:* The result may be an undefined address, but the operations otherwise have no undefined behavior.

```cpp
T* operator op=(difference_type operand) const noexcept;
```

**7** *Effects:* Equivalent to: `return fetch_key(operand) op operand;`
