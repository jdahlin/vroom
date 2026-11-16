# 24.5.2.1 Members [view.interface.members]

[Contents](../../README.md) · up: [24.5.2 View interface \[view.interface\]](view.interface.md) · prev: [24.5.2 View interface \[view.interface\]](view.interface.md) · next: [24.5.3 Sub-ranges \[range.subrange\]](range.subrange.md)

```cpp
constexpr decltype(auto) front() requires forward_range<D>;
constexpr decltype(auto) front() const requires forward_range<const D>;
```

**1** *Preconditions:* `!empty()`.

**2** *Effects:* Equivalent to: `return *ranges::begin(derived());`

```cpp
constexpr decltype(auto) back() requires bidirectional_range<D> && common_range<D>;
constexpr decltype(auto) back() const
  requires bidirectional_range<const D> && common_range<const D>;
```

**3** *Preconditions:* `!empty()`.

**4** *Effects:* Equivalent to: `return *ranges::prev(ranges::end(derived()));`
