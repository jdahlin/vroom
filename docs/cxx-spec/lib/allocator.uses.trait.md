# 20.10.8.1 `uses_allocator` trait [allocator.uses.trait]

[Contents](../README.md) · up: [20.10.8 `uses_allocator` \[allocator.uses\]](allocator.uses.md) · prev: [20.10.8 `uses_allocator` \[allocator.uses\]](allocator.uses.md) · next: [20.10.8.2 Uses-allocator construction \[allocator.uses.construction\]](allocator.uses.construction.md)

```cpp
template<class T, class Alloc> struct uses_allocator;
```

**1** *Remarks:* Automatically detects whether `T` has a nested `allocator_type` that is convertible from `Alloc`. Meets the *Cpp17BinaryTypeTrait* requirements ([[meta.rqmts]](meta.rqmts.md)). The implementation shall provide a definition that is derived from `true_type` if the *qualified-id* `T::allocator_type` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)) and `is_convertible_v<Alloc, T::allocator_type> != false`, otherwise it shall be derived from `false_type`. A program may specialize this template to derive from `true_type` for a program-defined type `T` that does not have a nested `allocator_type` but nonetheless can be constructed with an allocator where either:

- the first argument of a constructor has type `allocator_arg_t` and the second argument has type `Alloc` or
- the last argument of a constructor has type `Alloc`.
