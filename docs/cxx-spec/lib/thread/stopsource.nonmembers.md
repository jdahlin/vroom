# 32.3.4.3 Non-member functions [stopsource.nonmembers]

[Contents](../../README.md) · up: [32.3.4 Class `stop_source` \[stopsource\]](stopsource.md) · prev: [32.3.4.2 Members \[stopsource.mem\]](stopsource.mem.md) · next: [32.3.5 Class template `stop_callback` \[stopcallback\]](stopcallback.md)

```cpp
[[nodiscard]] friend bool
  operator==(const stop_source& lhs, const stop_source& rhs) noexcept;
```

**1** *Returns:* `true` if `lhs` and `rhs` have ownership of the same stop state or if both `lhs` and `rhs` do not have ownership of a stop state; otherwise `false`.

```cpp
friend void swap(stop_source& x, stop_source& y) noexcept;
```

**2** *Effects:* Equivalent to: `x.swap(y)`.
