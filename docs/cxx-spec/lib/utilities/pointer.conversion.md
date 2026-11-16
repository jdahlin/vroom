# 20.10.4 Pointer conversion [pointer.conversion]

[Contents](../../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.3.3 Optional members \[pointer.traits.optmem\]](pointer.traits.optmem.md) · next: [20.10.5 Pointer safety \[util.dynamic.safety\]](util.dynamic.safety.md)

```cpp
template<class T> constexpr T* to_address(T* p) noexcept;
```

**1** *Mandates:* `T` is not a function type.

**2** *Returns:* `p`.

```cpp
template<class Ptr> constexpr auto to_address(const Ptr& p) noexcept;
```

**3** *Returns:* `pointer_traits<Ptr>::to_address(p)` if that expression is well-formed (see [[pointer.traits.optmem]](pointer.traits.optmem.md)), otherwise `to_address(p.operator->())`.
