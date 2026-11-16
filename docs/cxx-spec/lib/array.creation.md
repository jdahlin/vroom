# 22.3.7.6 Array creation functions [array.creation]

[Contents](../README.md) · up: [22.3.7 Class template `array` \[array\]](array.md) · prev: [22.3.7.5 Zero-sized arrays \[array.zero\]](array.zero.md) · next: [22.3.7.7 Tuple interface \[array.tuple\]](array.tuple.md)

```cpp
template<class T, size_t N>
  constexpr array<remove_cv_t<T>, N> to_array(T (&a)[N]);
```

**1** *Mandates:* `is_array_v<T>` is `false` and `is_constructible_v<T, T&>` is `true`.

**2** *Preconditions:* `T` meets the *Cpp17CopyConstructible* requirements.

**3** *Returns:* `{{ a[0], ..., a[N - 1] }}`.

```cpp
template<class T, size_t N>
  constexpr array<remove_cv_t<T>, N> to_array(T (&&a)[N]);
```

**4** *Mandates:* `is_array_v<T>` is `false` and `is_move_constructible_v<T>` is `true`.

**5** *Preconditions:* `T` meets the *Cpp17MoveConstructible* requirements.

**6** *Returns:* `{{ std::move(a[0]), ..., std::move(a[N - 1]) }}`.
