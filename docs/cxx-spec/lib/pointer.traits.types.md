# 20.10.3.1 Member types [pointer.traits.types]

[Contents](../README.md) · up: [20.10.3 Pointer traits \[pointer.traits\]](pointer.traits.md) · prev: [20.10.3 Pointer traits \[pointer.traits\]](pointer.traits.md) · next: [20.10.3.2 Member functions \[pointer.traits.functions\]](pointer.traits.functions.md)

```cpp
using element_type = see below;
```

**1** *Type:* `Ptr::element_type` if the *qualified-id* `Ptr::element_type` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `T` if `Ptr` is a class template instantiation of the form `SomePointer<T, Args>`, where `Args` is zero or more type arguments; otherwise, the specialization is ill-formed.

```cpp
using difference_type = see below;
```

**2** *Type:* `Ptr::difference_type` if the *qualified-id* `Ptr::difference_type` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `ptrdiff_t`.

```cpp
template<class U> using rebind = see below;
```

**3** *Alias template:* `Ptr::rebind<U>` if the *qualified-id* `Ptr::rebind<U>` is valid and denotes a type ([[temp.deduct]](../temp.deduct.md)); otherwise, `SomePointer<U, Args>` if `Ptr` is a class template instantiation of the form `SomePointer<T, Args>`, where `Args` is zero or more type arguments; otherwise, the instantiation of `rebind` is ill-formed.
