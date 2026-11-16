# 32.3.3.2 Members [stoptoken.mem]

[Contents](../README.md) · up: [32.3.3 Class `stop_token` \[stoptoken\]](stoptoken.md) · prev: [32.3.3.1 Constructors, copy, and assignment \[stoptoken.cons\]](stoptoken.cons.md) · next: [32.3.3.3 Non-member functions \[stoptoken.nonmembers\]](stoptoken.nonmembers.md)

```cpp
[[nodiscard]] bool stop_requested() const noexcept;
```

**1** *Returns:* `true` if `*this` has ownership of a stop state that has received a stop request; otherwise, `false`.

```cpp
[[nodiscard]] bool stop_possible() const noexcept;
```

**2** *Returns:* `false` if:

- `*this` does not have ownership of a stop state, or
- a stop request was not made and there are no associated `stop_source` objects;

otherwise, `true`.
