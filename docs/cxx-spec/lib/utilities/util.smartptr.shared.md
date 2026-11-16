# 20.11.3 Class template `shared_ptr` [util.smartptr.shared]

[Contents](../../README.md) · up: [20.11 Smart pointers \[smartptr\]](smartptr.md) · prev: [20.11.2 Class `bad_weak_ptr` \[util.smartptr.weak.bad\]](util.smartptr.weak.bad.md) · next: [20.11.3.1 Constructors \[util.smartptr.shared.const\]](util.smartptr.shared.const.md)

**1** The `shared_ptr` class template stores a pointer, usually obtained via `new`. `shared_ptr` implements semantics of shared ownership; the last remaining owner of the pointer is responsible for destroying the object, or otherwise releasing the resources associated with the stored pointer. A `shared_ptr` is said to be empty if it does not own a pointer.

```cpp
namespace std {
  template<class T> class shared_ptr {
  public:
    using element_type = remove_extent_t<T>;
    using weak_type    = weak_ptr<T>;

    // [util.smartptr.shared.const], constructors
    constexpr shared_ptr() noexcept;
    constexpr shared_ptr(nullptr_t) noexcept : shared_ptr() { }
    template<class Y>
      explicit shared_ptr(Y* p);
    template<class Y, class D>
      shared_ptr(Y* p, D d);
    template<class Y, class D, class A>
      shared_ptr(Y* p, D d, A a);
    template<class D>
      shared_ptr(nullptr_t p, D d);
    template<class D, class A>
      shared_ptr(nullptr_t p, D d, A a);
    template<class Y>
      shared_ptr(const shared_ptr<Y>& r, element_type* p) noexcept;
    template<class Y>
      shared_ptr(shared_ptr<Y>&& r, element_type* p) noexcept;
    shared_ptr(const shared_ptr& r) noexcept;
    template<class Y>
      shared_ptr(const shared_ptr<Y>& r) noexcept;
    shared_ptr(shared_ptr&& r) noexcept;
    template<class Y>
      shared_ptr(shared_ptr<Y>&& r) noexcept;
    template<class Y>
      explicit shared_ptr(const weak_ptr<Y>& r);
    template<class Y, class D>
      shared_ptr(unique_ptr<Y, D>&& r);

    // [util.smartptr.shared.dest], destructor
    ~shared_ptr();

    // [util.smartptr.shared.assign], assignment
    shared_ptr& operator=(const shared_ptr& r) noexcept;
    template<class Y>
      shared_ptr& operator=(const shared_ptr<Y>& r) noexcept;
    shared_ptr& operator=(shared_ptr&& r) noexcept;
    template<class Y>
      shared_ptr& operator=(shared_ptr<Y>&& r) noexcept;
    template<class Y, class D>
      shared_ptr& operator=(unique_ptr<Y, D>&& r);

    // [util.smartptr.shared.mod], modifiers
    void swap(shared_ptr& r) noexcept;
    void reset() noexcept;
    template<class Y>
      void reset(Y* p);
    template<class Y, class D>
      void reset(Y* p, D d);
    template<class Y, class D, class A>
      void reset(Y* p, D d, A a);

    // [util.smartptr.shared.obs], observers
    element_type* get() const noexcept;
    T& operator*() const noexcept;
    T* operator->() const noexcept;
    element_type& operator[](ptrdiff_t i) const;
    long use_count() const noexcept;
    explicit operator bool() const noexcept;
    template<class U>
      bool owner_before(const shared_ptr<U>& b) const noexcept;
    template<class U>
      bool owner_before(const weak_ptr<U>& b) const noexcept;
  };

  template<class T>
    shared_ptr(weak_ptr<T>) -> shared_ptr<T>;
  template<class T, class D>
    shared_ptr(unique_ptr<T, D>) -> shared_ptr<T>;
}
```

**2** Specializations of `shared_ptr` shall be *Cpp17CopyConstructible*, *Cpp17CopyAssignable*, and *Cpp17LessThanComparable*, allowing their use in standard containers. Specializations of `shared_ptr` shall be contextually convertible to `bool`, allowing their use in boolean expressions and declarations in conditions.

**3** The template parameter `T` of `shared_ptr` may be an incomplete type. [*Note 1*: `T` may be a function type. — *end note*]

**4** [*Example 1*:

```cpp
if (shared_ptr<X> px = dynamic_pointer_cast<X>(py)) {
  // do something with px
}
```

— *end example*]

**5** For purposes of determining the presence of a data race, member functions shall access and modify only the `shared_ptr` and `weak_ptr` objects themselves and not objects they refer to. Changes in `use_count()` do not reflect modifications that can introduce data races.

**6** For the purposes of subclause [[smartptr]](smartptr.md), a pointer type `Y*` is said to be *compatible with* a pointer type `T*` when either `Y*` is convertible to `T*` or `Y` is `U[N]` and `T` is *cv* `U[]`.

## Subclauses

- [20.11.3.1 Constructors \[util.smartptr.shared.const\]](util.smartptr.shared.const.md)
- [20.11.3.2 Destructor \[util.smartptr.shared.dest\]](util.smartptr.shared.dest.md)
- [20.11.3.3 Assignment \[util.smartptr.shared.assign\]](util.smartptr.shared.assign.md)
- [20.11.3.4 Modifiers \[util.smartptr.shared.mod\]](util.smartptr.shared.mod.md)
- [20.11.3.5 Observers \[util.smartptr.shared.obs\]](util.smartptr.shared.obs.md)
- [20.11.3.6 Creation \[util.smartptr.shared.create\]](util.smartptr.shared.create.md)
- [20.11.3.7 Comparison \[util.smartptr.shared.cmp\]](util.smartptr.shared.cmp.md)
- [20.11.3.8 Specialized algorithms \[util.smartptr.shared.spec\]](util.smartptr.shared.spec.md)
- [20.11.3.9 Casts \[util.smartptr.shared.cast\]](util.smartptr.shared.cast.md)
- [20.11.3.10 `get_deleter` \[util.smartptr.getdeleter\]](util.smartptr.getdeleter.md)
- [20.11.3.11 I/O \[util.smartptr.shared.io\]](util.smartptr.shared.io.md)
