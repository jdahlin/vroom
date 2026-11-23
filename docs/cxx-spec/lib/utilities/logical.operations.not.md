# 20.14.9.3 Class template `logical_not` [logical.operations.not]

[Contents](../../README.md) · up: [20.14.9 Logical operations \[logical.operations\]](logical.operations.md) · prev: [20.14.9.2 Class template `logical_or` \[logical.operations.or\]](logical.operations.or.md) · next: [20.14.10 Bitwise operations \[bitwise.operations\]](bitwise.operations.md)

```cpp
template<class T = void> struct logical_not {
  constexpr bool operator()(const T& x) const;
};
```

```cpp
constexpr bool operator()(const T& x) const;
```

**1** *Returns:* `!x`.

```cpp
template<> struct logical_not<void> {
  template<class T> constexpr auto operator()(T&& t) const
    -> decltype(!std::forward<T>(t));

  using is_transparent = unspecified;
};
```

```cpp
template<class T> constexpr auto operator()(T&& t) const
    -> decltype(!std::forward<T>(t));
```

**2** *Returns:* `!std::forward<T>(t)`.
