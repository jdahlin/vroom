# 20.6.10 Hash support [optional.hash]

[Contents](../../README.md) · up: [20.6 Optional objects \[optional\]](optional.md) · prev: [20.6.9 Specialized algorithms \[optional.specalg\]](optional.specalg.md) · next: [20.7 Variants \[variant\]](variant.md)

```cpp
template<class T> struct hash<optional<T>>;
```

**1** The specialization `hash<optional<T>>` is enabled ([[unord.hash]](unord.hash.md)) if and only if `hash<remove_const_t<T>>` is enabled. When enabled, for an object `o` of type `optional<T>`, if `bool(o) == true`, then `hash<optional<T>>()(o)` evaluates to the same value as `hash<remove_const_t<T>>()(*o)`; otherwise it evaluates to an unspecified value. The member functions are not guaranteed to be `noexcept`.
