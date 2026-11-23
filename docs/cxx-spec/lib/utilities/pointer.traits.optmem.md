# 20.10.3.3 Optional members [pointer.traits.optmem]

[Contents](../../README.md) · up: [20.10.3 Pointer traits \[pointer.traits\]](pointer.traits.md) · prev: [20.10.3.2 Member functions \[pointer.traits.functions\]](pointer.traits.functions.md) · next: [20.10.4 Pointer conversion \[pointer.conversion\]](pointer.conversion.md)

**1** Specializations of `pointer_traits` may define the member declared in this subclause to customize the behavior of the standard library.

```cpp
static element_type* to_address(pointer p) noexcept;
```

**2** *Returns:* A pointer of type `element_type*` that references the same location as the argument `p`.

**3** [*Note 1*: This function should be the inverse of `pointer_to`. If defined, it customizes the behavior of the non-member function `to_address` ([[pointer.conversion]](pointer.conversion.md)). — *end note*]
