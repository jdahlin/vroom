# 20.11.4.5 Observers [util.smartptr.weak.obs]

[Contents](../../README.md) · up: [20.11.4 Class template `weak_ptr` \[util.smartptr.weak\]](util.smartptr.weak.md) · prev: [20.11.4.4 Modifiers \[util.smartptr.weak.mod\]](util.smartptr.weak.mod.md) · next: [20.11.4.6 Specialized algorithms \[util.smartptr.weak.spec\]](util.smartptr.weak.spec.md)

```cpp
long use_count() const noexcept;
```

**1** *Returns:* `0` if `*this` is empty; otherwise, the number of `shared_ptr` instances that share ownership with `*this`.

```cpp
bool expired() const noexcept;
```

**2** *Returns:* `use_count() == 0`.

```cpp
shared_ptr<T> lock() const noexcept;
```

**3** *Returns:* `expired() ? shared_ptr<T>(): shared_ptr<T>(*this)`, executed atomically.

```cpp
template<class U> bool owner_before(const shared_ptr<U>& b) const noexcept;
template<class U> bool owner_before(const weak_ptr<U>& b) const noexcept;
```

**4** *Returns:* An unspecified value such that

- `x.owner_before(y)` defines a strict weak ordering as defined in [[alg.sorting]](../algorithms/alg.sorting.md);
- under the equivalence relation defined by `owner_before`, `!a.owner_before(b) && !b.owner_before(a)`, two `shared_ptr` or `weak_ptr` instances are equivalent if and only if they share ownership or are both empty.
