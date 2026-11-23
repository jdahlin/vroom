# 20.2.5 Function template `as_const` [utility.as.const]

[Contents](../../README.md) · up: [20.2 Utility components \[utility\]](utility.md) · prev: [20.2.4 Forward/move helpers \[forward\]](forward.md) · next: [20.2.6 Function template `declval` \[declval\]](declval.md)

```cpp
template<class T> constexpr add_const_t<T>& as_const(T& t) noexcept;
```

**1** *Returns:* `t`.
