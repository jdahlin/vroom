# 31.8 Class template `atomic` [atomics.types.generic]

[Contents](../../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31.7.5 Member operators common to integers and pointers to objects \[atomics.ref.memop\]](atomics.ref.memop.md) · next: [31.8.1 Operations on atomic types \[atomics.types.operations\]](atomics.types.operations.md)

```cpp
namespace std {
  template<class T> struct atomic {
    using value_type = T;

    static constexpr bool is_always_lock_free = implementation-defined;
    bool is_lock_free() const volatile noexcept;
    bool is_lock_free() const noexcept;

    // [atomics.types.operations], operations on atomic types
    constexpr atomic() noexcept(is_nothrow_default_constructible_v<T>);
    constexpr atomic(T) noexcept;
    atomic(const atomic&) = delete;
    atomic& operator=(const atomic&) = delete;
    atomic& operator=(const atomic&) volatile = delete;

    T load(memory_order = memory_order::seq_cst) const volatile noexcept;
    T load(memory_order = memory_order::seq_cst) const noexcept;
    operator T() const volatile noexcept;
    operator T() const noexcept;
    void store(T, memory_order = memory_order::seq_cst) volatile noexcept;
    void store(T, memory_order = memory_order::seq_cst) noexcept;
    T operator=(T) volatile noexcept;
    T operator=(T) noexcept;

    T exchange(T, memory_order = memory_order::seq_cst) volatile noexcept;
    T exchange(T, memory_order = memory_order::seq_cst) noexcept;
    bool compare_exchange_weak(T&, T, memory_order, memory_order) volatile noexcept;
    bool compare_exchange_weak(T&, T, memory_order, memory_order) noexcept;
    bool compare_exchange_strong(T&, T, memory_order, memory_order) volatile noexcept;
    bool compare_exchange_strong(T&, T, memory_order, memory_order) noexcept;
    bool compare_exchange_weak(T&, T, memory_order = memory_order::seq_cst) volatile noexcept;
    bool compare_exchange_weak(T&, T, memory_order = memory_order::seq_cst) noexcept;
    bool compare_exchange_strong(T&, T, memory_order = memory_order::seq_cst) volatile noexcept;
    bool compare_exchange_strong(T&, T, memory_order = memory_order::seq_cst) noexcept;

    void wait(T, memory_order = memory_order::seq_cst) const volatile noexcept;
    void wait(T, memory_order = memory_order::seq_cst) const noexcept;
    void notify_one() volatile noexcept;
    void notify_one() noexcept;
    void notify_all() volatile noexcept;
    void notify_all() noexcept;
  };
}
```

**1** The template argument for `T` shall meet the *Cpp17CopyConstructible* and *Cpp17CopyAssignable* requirements. The program is ill-formed if any of

- `is_trivially_copyable_v<T>`,
- `is_copy_constructible_v<T>`,
- `is_move_constructible_v<T>`,
- `is_copy_assignable_v<T>`, or
- `is_move_assignable_v<T>`

is `false`. [*Note 1*: Type arguments that are not also statically initializable may be difficult to use. — *end note*]

**2** The specialization `atomic<bool>` is a standard-layout struct.

**3** [*Note 2*: The representation of an atomic specialization need not have the same size and alignment requirement as its corresponding argument type. — *end note*]

## Subclauses

- [31.8.1 Operations on atomic types \[atomics.types.operations\]](atomics.types.operations.md)
- [31.8.2 Specializations for integers \[atomics.types.int\]](atomics.types.int.md)
- [31.8.3 Specializations for floating-point types \[atomics.types.float\]](atomics.types.float.md)
- [31.8.4 Partial specialization for pointers \[atomics.types.pointer\]](atomics.types.pointer.md)
- [31.8.5 Member operators common to integers and pointers to objects \[atomics.types.memop\]](atomics.types.memop.md)
- [31.8.6 Partial specializations for smart pointers \[util.smartptr.atomic\]](util.smartptr.atomic.md)
  - [31.8.6.1 Partial specialization for `shared_ptr` \[util.smartptr.atomic.shared\]](util.smartptr.atomic.shared.md)
  - [31.8.6.2 Partial specialization for `weak_ptr` \[util.smartptr.atomic.weak\]](util.smartptr.atomic.weak.md)
