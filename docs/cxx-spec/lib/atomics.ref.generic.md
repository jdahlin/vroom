# 31.7 Class template `atomic_ref` [atomics.ref.generic]

[Contents](../README.md) · up: [31 Atomic operations library \[atomics\]](atomics.md) · prev: [31.6 Waiting and notifying \[atomics.wait\]](atomics.wait.md) · next: [31.7.1 Operations \[atomics.ref.ops\]](atomics.ref.ops.md)

```cpp
namespace std {
  template<class T> struct atomic_ref {
  private:
    T* ptr;             // exposition only
  public:
    using value_type = T;
    static constexpr size_t required_alignment = implementation-defined;

    static constexpr bool is_always_lock_free = implementation-defined;
    bool is_lock_free() const noexcept;

    explicit atomic_ref(T&);
    atomic_ref(const atomic_ref&) noexcept;
    atomic_ref& operator=(const atomic_ref&) = delete;

    void store(T, memory_order = memory_order::seq_cst) const noexcept;
    T operator=(T) const noexcept;
    T load(memory_order = memory_order::seq_cst) const noexcept;
    operator T() const noexcept;

    T exchange(T, memory_order = memory_order::seq_cst) const noexcept;
    bool compare_exchange_weak(T&, T,
                               memory_order, memory_order) const noexcept;
    bool compare_exchange_strong(T&, T,
                                 memory_order, memory_order) const noexcept;
    bool compare_exchange_weak(T&, T,
                               memory_order = memory_order::seq_cst) const noexcept;
    bool compare_exchange_strong(T&, T,
                                 memory_order = memory_order::seq_cst) const noexcept;

    void wait(T, memory_order = memory_order::seq_cst) const noexcept;
    void notify_one() const noexcept;
    void notify_all() const noexcept;
  };
}
```

**1** An `atomic_ref` object applies atomic operations ([[atomics.general]](atomics.general.md)) to the object referenced by `*ptr` such that, for the lifetime ([[basic.life]](../basic.life.md)) of the `atomic_ref` object, the object referenced by `*ptr` is an atomic object ([[intro.races]](../intro.races.md)).

**2** The program is ill-formed if `is_trivially_copyable_v<T>` is `false`.

**3** The lifetime ([[basic.life]](../basic.life.md)) of an object referenced by `*ptr` shall exceed the lifetime of all `atomic_ref`s that reference the object. While any `atomic_ref` instances exist that reference the `*ptr` object, all accesses to that object shall exclusively occur through those `atomic_ref` instances. No subobject of the object referenced by `atomic_ref` shall be concurrently referenced by any other `atomic_ref` object.

**4** Atomic operations applied to an object through a referencing `atomic_ref` are atomic with respect to atomic operations applied through any other `atomic_ref` referencing the same object. [*Note 1*: Atomic operations or the `atomic_ref` constructor could acquire a shared resource, such as a lock associated with the referenced object, to enable atomic operations to be applied to the referenced object. — *end note*]

## Subclauses

- [31.7.1 Operations \[atomics.ref.ops\]](atomics.ref.ops.md)
- [31.7.2 Specializations for integral types \[atomics.ref.int\]](atomics.ref.int.md)
- [31.7.3 Specializations for floating-point types \[atomics.ref.float\]](atomics.ref.float.md)
- [31.7.4 Partial specialization for pointers \[atomics.ref.pointer\]](atomics.ref.pointer.md)
- [31.7.5 Member operators common to integers and pointers to objects \[atomics.ref.memop\]](atomics.ref.memop.md)
