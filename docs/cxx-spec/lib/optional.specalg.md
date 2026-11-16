# 20.6.9 Specialized algorithms [optional.specalg]

[Contents](../README.md) · up: [20.6 Optional objects \[optional\]](optional.md) · prev: [20.6.8 Comparison with `T` \[optional.comp.with.t\]](optional.comp.with.t.md) · next: [20.6.10 Hash support \[optional.hash\]](optional.hash.md)

```cpp
template<class T> void swap(optional<T>& x, optional<T>& y) noexcept(noexcept(x.swap(y)));
```

**1** *Constraints:* `is_move_constructible_v<T>` is `true` and `is_swappable_v<T>` is `true`.

**2** *Effects:* Calls `x.swap(y)`.

```cpp
template<class T> constexpr optional<decay_t<T>> make_optional(T&& v);
```

**3** *Returns:* `optional<decay_t<T>>(std::forward<T>(v))`.

```cpp
template<class T, class...Args>
  constexpr optional<T> make_optional(Args&&... args);
```

**4** *Effects:* Equivalent to: `return optional<T>(in_place, std::forward<Args>(args)...);`

```cpp
template<class T, class U, class... Args>
  constexpr optional<T> make_optional(initializer_list<U> il, Args&&... args);
```

**5** *Effects:* Equivalent to: `return optional<T>(in_place, il, std::forward<Args>(args)...);`
