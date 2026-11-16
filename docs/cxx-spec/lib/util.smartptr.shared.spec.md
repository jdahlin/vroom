# 20.11.3.8 Specialized algorithms [util.smartptr.shared.spec]

[Contents](../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.7 Comparison \[util.smartptr.shared.cmp\]](util.smartptr.shared.cmp.md) · next: [20.11.3.9 Casts \[util.smartptr.shared.cast\]](util.smartptr.shared.cast.md)

```cpp
template<class T>
  void swap(shared_ptr<T>& a, shared_ptr<T>& b) noexcept;
```

**1** *Effects:* Equivalent to `a.swap(b)`.
