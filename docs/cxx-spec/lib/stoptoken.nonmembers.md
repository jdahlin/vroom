# 32.3.3.3 Non-member functions [stoptoken.nonmembers]

[Contents](../README.md) · up: [32.3.3 Class `stop_token` \[stoptoken\]](stoptoken.md) · prev: [32.3.3.2 Members \[stoptoken.mem\]](stoptoken.mem.md) · next: [32.3.4 Class `stop_source` \[stopsource\]](stopsource.md)

```cpp
[[nodiscard]] bool operator==(const stop_token& lhs, const stop_token& rhs) noexcept;
```

**1** *Returns:* `true` if `lhs` and `rhs` have ownership of the same stop state or if both `lhs` and `rhs` do not have ownership of a stop state; otherwise `false`.

```cpp
friend void swap(stop_token& x, stop_token& y) noexcept;
```

**2** *Effects:* Equivalent to: `x.swap(y)`.
