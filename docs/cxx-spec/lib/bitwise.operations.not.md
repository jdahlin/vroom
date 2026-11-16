# 20.14.10.4 Class template `bit_not` [bitwise.operations.not]

[Contents](../README.md) · up: [20.14.10 Bitwise operations \[bitwise.operations\]](bitwise.operations.md) · prev: [20.14.10.3 Class template `bit_xor` \[bitwise.operations.xor\]](bitwise.operations.xor.md) · next: [20.14.11 Class `identity` \[func.identity\]](func.identity.md)

```cpp
template<class T = void> struct bit_not {
  constexpr T operator()(const T& x) const;
};
```

```cpp
constexpr T operator()(const T& x) const;
```

**1** *Returns:* `~x`.

```cpp
template<> struct bit_not<void> {
  template<class T> constexpr auto operator()(T&& t) const
    -> decltype(~std::forward<T>(t));

  using is_transparent = unspecified;
};
```

```cpp
template<class T> constexpr auto operator()(T&&) const
    -> decltype(~std::forward<T>(t));
```

**2** *Returns:* `~std::forward<T>(t)`.
