# 20.11.4.3 Assignment [util.smartptr.weak.assign]

[Contents](../../README.md) · up: [20.11.4 Class template `weak_ptr` \[util.smartptr.weak\]](util.smartptr.weak.md) · prev: [20.11.4.2 Destructor \[util.smartptr.weak.dest\]](util.smartptr.weak.dest.md) · next: [20.11.4.4 Modifiers \[util.smartptr.weak.mod\]](util.smartptr.weak.mod.md)

```cpp
weak_ptr& operator=(const weak_ptr& r) noexcept;
template<class Y> weak_ptr& operator=(const weak_ptr<Y>& r) noexcept;
template<class Y> weak_ptr& operator=(const shared_ptr<Y>& r) noexcept;
```

**1** *Effects:* Equivalent to `weak_ptr(r).swap(*this)`.

**2** *Remarks:* The implementation may meet the effects (and the implied guarantees) via different means, without creating a temporary object.

**3** *Returns:* `*this`.

```cpp
weak_ptr& operator=(weak_ptr&& r) noexcept;
template<class Y> weak_ptr& operator=(weak_ptr<Y>&& r) noexcept;
```

**4** *Effects:* Equivalent to `weak_ptr(std::move(r)).swap(*this)`.

**5** *Returns:* `*this`.
