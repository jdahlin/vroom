# 20.5.10 Tuple specialized algorithms [tuple.special]

[Contents](../README.md) · up: [20.5 Tuples \[tuple\]](tuple.md) · prev: [20.5.9 Tuple traits \[tuple.traits\]](tuple.traits.md) · next: [20.6 Optional objects \[optional\]](optional.md)

```cpp
template<class... Types>
  constexpr void swap(tuple<Types...>& x, tuple<Types...>& y) noexcept(see below);
```

**1** *Constraints:* `is_swappable_v<T>` is `true` for every type `T` in `Types`.

**2** *Remarks:* The expression inside `noexcept` is equivalent to:

```cpp
noexcept(x.swap(y))
```

**3** *Effects:* As if by `x.swap(y)`.
