# 20.11.1.3 `unique_ptr` for array objects with a runtime length [unique.ptr.runtime]

[Contents](../../README.md) · up: [20.11.1 Class template `unique_ptr` \[unique.ptr\]](unique.ptr.md) · prev: [20.11.1.2.5 Modifiers \[unique.ptr.single.modifiers\]](unique.ptr.single.modifiers.md) · next: [20.11.1.3.1 Constructors \[unique.ptr.runtime.ctor\]](unique.ptr.runtime.ctor.md)

```cpp
namespace std {
  template<class T, class D> class unique_ptr<T[], D> {
  public:
    using pointer      = see below;
    using element_type = T;
    using deleter_type = D;

    // [unique.ptr.runtime.ctor], constructors
    constexpr unique_ptr() noexcept;
    template<class U> explicit unique_ptr(U p) noexcept;
    template<class U> unique_ptr(U p, see below d) noexcept;
    template<class U> unique_ptr(U p, see below d) noexcept;
    unique_ptr(unique_ptr&& u) noexcept;
    template<class U, class E>
      unique_ptr(unique_ptr<U, E>&& u) noexcept;
    constexpr unique_ptr(nullptr_t) noexcept;

    // destructor
    ~unique_ptr();

    // assignment
    unique_ptr& operator=(unique_ptr&& u) noexcept;
    template<class U, class E>
      unique_ptr& operator=(unique_ptr<U, E>&& u) noexcept;
    unique_ptr& operator=(nullptr_t) noexcept;

    // [unique.ptr.runtime.observers], observers
    T& operator[](size_t i) const;
    pointer get() const noexcept;
    deleter_type& get_deleter() noexcept;
    const deleter_type& get_deleter() const noexcept;
    explicit operator bool() const noexcept;

    // [unique.ptr.runtime.modifiers], modifiers
    pointer release() noexcept;
    template<class U> void reset(U p) noexcept;
    void reset(nullptr_t = nullptr) noexcept;
    void swap(unique_ptr& u) noexcept;

    // disable copy from lvalue
    unique_ptr(const unique_ptr&) = delete;
    unique_ptr& operator=(const unique_ptr&) = delete;
  };
}
```

**1** A specialization for array types is provided with a slightly altered interface.

- Conversions between different types of `unique_ptr<T[], D>` that would be disallowed for the corresponding pointer-to-array types, and conversions to or from the non-array forms of `unique_ptr`, produce an ill-formed program.
- Pointers to types derived from `T` are rejected by the constructors, and by `reset`.
- The observers `operator*` and `operator->` are not provided.
- The indexing observer `operator[]` is provided.
- The default deleter will call `delete[]`.

**2** Descriptions are provided below only for members that differ from the primary template.

**3** The template argument `T` shall be a complete type.

## Subclauses

- [20.11.1.3.1 Constructors \[unique.ptr.runtime.ctor\]](unique.ptr.runtime.ctor.md)
- [20.11.1.3.2 Assignment \[unique.ptr.runtime.asgn\]](unique.ptr.runtime.asgn.md)
- [20.11.1.3.3 Observers \[unique.ptr.runtime.observers\]](unique.ptr.runtime.observers.md)
- [20.11.1.3.4 Modifiers \[unique.ptr.runtime.modifiers\]](unique.ptr.runtime.modifiers.md)
