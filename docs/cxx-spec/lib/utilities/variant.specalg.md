# 20.7.10 Specialized algorithms [variant.specalg]

[Contents](../../README.md) · up: [20.7 Variants \[variant\]](variant.md) · prev: [20.7.9 `monostate` relational operators \[variant.monostate.relops\]](variant.monostate.relops.md) · next: [20.7.11 Class `bad_variant_access` \[variant.bad.access\]](variant.bad.access.md)

```cpp
template<class... Types>
  void swap(variant<Types...>& v, variant<Types...>& w) noexcept(see below);
```

**1** *Constraints:* `is_move_constructible_v<T_i> && is_swappable_v<T_i>` is `true` for all *i*.

**2** *Effects:* Equivalent to `v.swap(w)`.

**3** *Remarks:* The expression inside `noexcept` is equivalent to `noexcept(v.swap(w))`.
