# 20.11.4 Class template `weak_ptr` [util.smartptr.weak]

[Contents](../../README.md) · up: [20.11 Smart pointers \[smartptr\]](smartptr.md) · prev: [20.11.3.11 I/O \[util.smartptr.shared.io\]](util.smartptr.shared.io.md) · next: [20.11.4.1 Constructors \[util.smartptr.weak.const\]](util.smartptr.weak.const.md)

**1** The `weak_ptr` class template stores a weak reference to an object that is already managed by a `shared_ptr`. To access the object, a `weak_ptr` can be converted to a `shared_ptr` using the member function `lock`.

```cpp
namespace std {
  template<class T> class weak_ptr {
  public:
    using element_type = remove_extent_t<T>;

    // [util.smartptr.weak.const], constructors
    constexpr weak_ptr() noexcept;
    template<class Y>
      weak_ptr(const shared_ptr<Y>& r) noexcept;
    weak_ptr(const weak_ptr& r) noexcept;
    template<class Y>
      weak_ptr(const weak_ptr<Y>& r) noexcept;
    weak_ptr(weak_ptr&& r) noexcept;
    template<class Y>
      weak_ptr(weak_ptr<Y>&& r) noexcept;

    // [util.smartptr.weak.dest], destructor
    ~weak_ptr();

    // [util.smartptr.weak.assign], assignment
    weak_ptr& operator=(const weak_ptr& r) noexcept;
    template<class Y>
      weak_ptr& operator=(const weak_ptr<Y>& r) noexcept;
    template<class Y>
      weak_ptr& operator=(const shared_ptr<Y>& r) noexcept;
    weak_ptr& operator=(weak_ptr&& r) noexcept;
    template<class Y>
      weak_ptr& operator=(weak_ptr<Y>&& r) noexcept;

    // [util.smartptr.weak.mod], modifiers
    void swap(weak_ptr& r) noexcept;
    void reset() noexcept;

    // [util.smartptr.weak.obs], observers
    long use_count() const noexcept;
    bool expired() const noexcept;
    shared_ptr<T> lock() const noexcept;
    template<class U>
      bool owner_before(const shared_ptr<U>& b) const noexcept;
    template<class U>
      bool owner_before(const weak_ptr<U>& b) const noexcept;
  };

  template<class T>
    weak_ptr(shared_ptr<T>) -> weak_ptr<T>;

  // [util.smartptr.weak.spec], specialized algorithms
  template<class T>
    void swap(weak_ptr<T>& a, weak_ptr<T>& b) noexcept;
}
```

**2** Specializations of `weak_ptr` shall be *Cpp17CopyConstructible* and *Cpp17CopyAssignable*, allowing their use in standard containers. The template parameter `T` of `weak_ptr` may be an incomplete type.

## Subclauses

- [20.11.4.1 Constructors \[util.smartptr.weak.const\]](util.smartptr.weak.const.md)
- [20.11.4.2 Destructor \[util.smartptr.weak.dest\]](util.smartptr.weak.dest.md)
- [20.11.4.3 Assignment \[util.smartptr.weak.assign\]](util.smartptr.weak.assign.md)
- [20.11.4.4 Modifiers \[util.smartptr.weak.mod\]](util.smartptr.weak.mod.md)
- [20.11.4.5 Observers \[util.smartptr.weak.obs\]](util.smartptr.weak.obs.md)
- [20.11.4.6 Specialized algorithms \[util.smartptr.weak.spec\]](util.smartptr.weak.spec.md)
