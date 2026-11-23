# 20.10.3.2 Member functions [pointer.traits.functions]

[Contents](../../README.md) · up: [20.10.3 Pointer traits \[pointer.traits\]](pointer.traits.md) · prev: [20.10.3.1 Member types \[pointer.traits.types\]](pointer.traits.types.md) · next: [20.10.3.3 Optional members \[pointer.traits.optmem\]](pointer.traits.optmem.md)

```cpp
static pointer pointer_traits::pointer_to(see below r);
static constexpr pointer pointer_traits<T*>::pointer_to(see below r) noexcept;
```

**1** *Mandates:* For the first member function, `Ptr::pointer_to(r)` is well-formed.

**2** *Preconditions:* For the first member function, `Ptr::pointer_to(r)` returns a pointer to `r` through which indirection is valid.

**3** *Returns:* The first member function returns `Ptr::pointer_to(r)`. The second member function returns `addressof(r)`.

**4** *Remarks:* If `element_type` is *cv* `void`, the type of `r` is unspecified; otherwise, it is `element_type&`.
