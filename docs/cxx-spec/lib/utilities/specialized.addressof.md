# 20.10.11 `addressof` [specialized.addressof]

[Contents](../../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.10.2 Operators \[allocator.globals\]](allocator.globals.md) · next: [20.10.12 C library memory allocation \[c.malloc\]](c.malloc.md)

```cpp
template<class T> constexpr T* addressof(T& r) noexcept;
```

**1** *Returns:* The actual address of the object or function referenced by `r`, even in the presence of an overloaded `operator&`.

**2** *Remarks:* An expression `addressof(E)` is a constant subexpression ([[defns.const.subexpr]](../library/definitions.md#defns.const.subexpr)) if `E` is an lvalue constant subexpression.
