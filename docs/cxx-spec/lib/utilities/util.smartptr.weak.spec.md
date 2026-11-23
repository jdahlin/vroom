# 20.11.4.6 Specialized algorithms [util.smartptr.weak.spec]

[Contents](../../README.md) · up: [20.11.4 Class template `weak_ptr` \[util.smartptr.weak\]](util.smartptr.weak.md) · prev: [20.11.4.5 Observers \[util.smartptr.weak.obs\]](util.smartptr.weak.obs.md) · next: [20.11.5 Class template `owner_less` \[util.smartptr.ownerless\]](util.smartptr.ownerless.md)

```cpp
template<class T>
  void swap(weak_ptr<T>& a, weak_ptr<T>& b) noexcept;
```

**1** *Effects:* Equivalent to `a.swap(b)`.
