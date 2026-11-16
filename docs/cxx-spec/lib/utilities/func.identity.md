# 20.14.11 Class `identity` [func.identity]

[Contents](../../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.10.4 Class template `bit_not` \[bitwise.operations.not\]](bitwise.operations.not.md) · next: [20.14.12 Function template `not_fn` \[func.not.fn\]](func.not.fn.md)

```cpp
struct identity {
  template<class T>
    constexpr T&& operator()(T&& t) const noexcept;

  using is_transparent = unspecified;
};

template<class T>
  constexpr T&& operator()(T&& t) const noexcept;
```

**1** *Effects:* Equivalent to: `return std::forward<T>(t);`
