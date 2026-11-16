# 20.11.3.11 I/O [util.smartptr.shared.io]

[Contents](../README.md) · up: [20.11.3 Class template `shared_ptr` \[util.smartptr.shared\]](util.smartptr.shared.md) · prev: [20.11.3.10 `get_deleter` \[util.smartptr.getdeleter\]](util.smartptr.getdeleter.md) · next: [20.11.4 Class template `weak_ptr` \[util.smartptr.weak\]](util.smartptr.weak.md)

```cpp
template<class E, class T, class Y>
  basic_ostream<E, T>& operator<<(basic_ostream<E, T>& os, const shared_ptr<Y>& p);
```

**1** *Effects:* As if by: `os << p.get();`

**2** *Returns:* `os`.
