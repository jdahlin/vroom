# 20.11.1.2 `unique_ptr` for single objects [unique.ptr.single]

[Contents](../../README.md) · up: [20.11.1 Class template `unique_ptr` \[unique.ptr\]](unique.ptr.md) · prev: [20.11.1.1.3 `default_delete<T[]>` \[unique.ptr.dltr.dflt1\]](unique.ptr.dltr.dflt1.md) · next: [20.11.1.2.1 Constructors \[unique.ptr.single.ctor\]](unique.ptr.single.ctor.md)

```cpp
namespace std {
  template<class T, class D = default_delete<T>> class unique_ptr {
  public:
    using pointer      = see below;
    using element_type = T;
    using deleter_type = D;

    // [unique.ptr.single.ctor], constructors
    constexpr unique_ptr() noexcept;
    explicit unique_ptr(pointer p) noexcept;
    unique_ptr(pointer p, see below d1) noexcept;
    unique_ptr(pointer p, see below d2) noexcept;
    unique_ptr(unique_ptr&& u) noexcept;
    constexpr unique_ptr(nullptr_t) noexcept;
    template<class U, class E>
      unique_ptr(unique_ptr<U, E>&& u) noexcept;

    // [unique.ptr.single.dtor], destructor
    ~unique_ptr();

    // [unique.ptr.single.asgn], assignment
    unique_ptr& operator=(unique_ptr&& u) noexcept;
    template<class U, class E>
      unique_ptr& operator=(unique_ptr<U, E>&& u) noexcept;
    unique_ptr& operator=(nullptr_t) noexcept;

    // [unique.ptr.single.observers], observers
    add_lvalue_reference_t<T> operator*() const;
    pointer operator->() const noexcept;
    pointer get() const noexcept;
    deleter_type& get_deleter() noexcept;
    const deleter_type& get_deleter() const noexcept;
    explicit operator bool() const noexcept;

    // [unique.ptr.single.modifiers], modifiers
    pointer release() noexcept;
    void reset(pointer p = pointer()) noexcept;
    void swap(unique_ptr& u) noexcept;

    // disable copy from lvalue
    unique_ptr(const unique_ptr&) = delete;
    unique_ptr& operator=(const unique_ptr&) = delete;
  };
}
```

**1** The default type for the template parameter `D` is `default_delete`. A client-supplied template argument `D` shall be a function object type ([[function.objects]](function.objects.md)), lvalue reference to function, or lvalue reference to function object type for which, given a value `d` of type `D` and a value `ptr` of type `unique_ptr<T, D>::pointer`, the expression `d(ptr)` is valid and has the effect of disposing of the pointer as appropriate for that deleter.

**2** If the deleter’s type `D` is not a reference type, `D` shall meet the *Cpp17Destructible* requirements ([Table 32](../library/utility.arg.requirements.md#tab:cpp17.destructible)).

**3** If the *qualified-id* `remove_reference_t<D>::pointer` is valid and denotes a type ([[temp.deduct]](../../temp/temp.deduct.md)), then `unique_ptr<T, D>::pointer` shall be a synonym for `remove_reference_t<D>::pointer`. Otherwise `unique_ptr<T, D>::pointer` shall be a synonym for `element_type*`. The type `unique_ptr<T, D>::pointer` shall meet the *Cpp17NullablePointer* requirements ([Table 33](../library/nullablepointer.requirements.md#tab:cpp17.nullablepointer)).

**4** [*Example 1*: Given an allocator type `X` ([Table 36](../library/allocator.requirements.md#tab:cpp17.allocator)) and letting `A` be a synonym for `allocator_traits<X>`, the types `A::pointer`, `A::const_pointer`, `A::void_pointer`, and `A::const_void_pointer` may be used as `unique_ptr<T, D>::pointer`. — *end example*]

## Subclauses

- [20.11.1.2.1 Constructors \[unique.ptr.single.ctor\]](unique.ptr.single.ctor.md)
- [20.11.1.2.2 Destructor \[unique.ptr.single.dtor\]](unique.ptr.single.dtor.md)
- [20.11.1.2.3 Assignment \[unique.ptr.single.asgn\]](unique.ptr.single.asgn.md)
- [20.11.1.2.4 Observers \[unique.ptr.single.observers\]](unique.ptr.single.observers.md)
- [20.11.1.2.5 Modifiers \[unique.ptr.single.modifiers\]](unique.ptr.single.modifiers.md)
