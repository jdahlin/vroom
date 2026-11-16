# 20.11.5 Class template `owner_less` [util.smartptr.ownerless]

[Contents](../README.md) · up: [20.11 Smart pointers \[smartptr\]](smartptr.md) · prev: [20.11.4.6 Specialized algorithms \[util.smartptr.weak.spec\]](util.smartptr.weak.spec.md) · next: [20.11.6 Class template `enable_shared_from_this` \[util.smartptr.enab\]](util.smartptr.enab.md)

**1** The class template `owner_less` allows ownership-based mixed comparisons of shared and weak pointers.

```cpp
namespace std {
  template<class T = void> struct owner_less;

  template<class T> struct owner_less<shared_ptr<T>> {
    bool operator()(const shared_ptr<T>&, const shared_ptr<T>&) const noexcept;
    bool operator()(const shared_ptr<T>&, const weak_ptr<T>&) const noexcept;
    bool operator()(const weak_ptr<T>&, const shared_ptr<T>&) const noexcept;
  };

  template<class T> struct owner_less<weak_ptr<T>> {
    bool operator()(const weak_ptr<T>&, const weak_ptr<T>&) const noexcept;
    bool operator()(const shared_ptr<T>&, const weak_ptr<T>&) const noexcept;
    bool operator()(const weak_ptr<T>&, const shared_ptr<T>&) const noexcept;
  };

  template<> struct owner_less<void> {
    template<class T, class U>
      bool operator()(const shared_ptr<T>&, const shared_ptr<U>&) const noexcept;
    template<class T, class U>
      bool operator()(const shared_ptr<T>&, const weak_ptr<U>&) const noexcept;
    template<class T, class U>
      bool operator()(const weak_ptr<T>&, const shared_ptr<U>&) const noexcept;
    template<class T, class U>
      bool operator()(const weak_ptr<T>&, const weak_ptr<U>&) const noexcept;

    using is_transparent = unspecified;
  };
}
```

**2** `operator()(x, y)` returns `x.owner_before(y)`. [*Note 1*: Note that

- `operator()` defines a strict weak ordering as defined in [[alg.sorting]](alg.sorting.md);
- under the equivalence relation defined by `operator()`, `!operator()(a, b) && !operator()(b, a)`, two `shared_ptr` or `weak_ptr` instances are equivalent if and only if they share ownership or are both empty.

— *end note*]
