# 20.11.1.6 I/O [unique.ptr.io]

[Contents](../README.md) · up: [20.11.1 Class template `unique_ptr` \[unique.ptr\]](unique.ptr.md) · prev: [20.11.1.5 Specialized algorithms \[unique.ptr.special\]](unique.ptr.special.md) · next: [20.11.2 Class `bad_weak_ptr` \[util.smartptr.weak.bad\]](util.smartptr.weak.bad.md)

```cpp
template<class E, class T, class Y, class D>
  basic_ostream<E, T>& operator<<(basic_ostream<E, T>& os, const unique_ptr<Y, D>& p);
```

**1** *Constraints:* `os << p.get()` is a valid expression.

**2** *Effects:* Equivalent to: `os << p.get();`

**3** *Returns:* `os`.
