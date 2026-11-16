# 20.11.3.10 `get_deleter` [util.smartptr.getdeleter]

[Contents](../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.9 Casts \[util.smartptr.shared.cast\]](util.smartptr.shared.cast.md) · next: [20.11.3.11 I/O \[util.smartptr.shared.io\]](util.smartptr.shared.io.md)

```cpp
template<class D, class T>
  D* get_deleter(const shared_ptr<T>& p) noexcept;
```

**1** *Returns:* If `p` owns a deleter `d` of type cv-unqualified `D`, returns `addressof(d)`; otherwise returns `nullptr`. The returned pointer remains valid as long as there exists a `shared_ptr` instance that owns `d`. [*Note 1*: It is unspecified whether the pointer remains valid longer than that. This can happen if the implementation doesn’t destroy the deleter until all `weak_ptr` instances that share ownership with `p` have been destroyed. — *end note*]
