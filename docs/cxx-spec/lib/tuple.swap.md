# 20.5.3.3 `swap` [tuple.swap]

[Contents](../README.md) · up: [20.5.3 Class template `tuple` \[tuple.tuple\]](tuple.tuple.md) · prev: [20.5.3.2 Assignment \[tuple.assign\]](tuple.assign.md) · next: [20.5.4 Tuple creation functions \[tuple.creation\]](tuple.creation.md)

```cpp
constexpr void swap(tuple& rhs) noexcept(see below);
```

**1** *Preconditions:* Each element in `*this` is swappable with ([[swappable.requirements]](swappable.requirements.md)) the corresponding element in `rhs`.

**2** *Effects:* Calls `swap` for each element in `*this` and its corresponding element in `rhs`.

**3** *Remarks:* The expression inside `noexcept` is equivalent to the logical and of the following expressions:

```cpp
is_nothrow_swappable_v<T_i>
```

where `T`<sub>*i*</sub> is the *i*<sup></sup>*th* type in `Types`.

**4** *Throws:* Nothing unless one of the element-wise `swap` calls throws an exception.
