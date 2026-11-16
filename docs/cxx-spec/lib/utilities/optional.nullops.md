# 20.6.7 Comparison with `nullopt` [optional.nullops]

[Contents](../../README.md) · up: [20.6 Optional objects \[optional\]](optional.md) · prev: [20.6.6 Relational operators \[optional.relops\]](optional.relops.md) · next: [20.6.8 Comparison with `T` \[optional.comp.with.t\]](optional.comp.with.t.md)

```cpp
template<class T> constexpr bool operator==(const optional<T>& x, nullopt_t) noexcept;
```

**1** *Returns:* `!x`.

```cpp
template<class T> constexpr strong_ordering operator<=>(const optional<T>& x, nullopt_t) noexcept;
```

**2** *Returns:* `bool(x) <=> false`.
