# 19.5.2.3 Non-virtual members [syserr.errcat.nonvirtuals]

[Contents](../README.md) · up: [19.5.2 Class `error_category` \[syserr.errcat\]](syserr.errcat.md) · prev: [19.5.2.2 Virtual members \[syserr.errcat.virtuals\]](syserr.errcat.virtuals.md) · next: [19.5.2.4 Program-defined classes derived from `error_category` \[syserr.errcat.derived\]](syserr.errcat.derived.md)

```cpp
bool operator==(const error_category& rhs) const noexcept;
```

**1** *Returns:* `this == &rhs`.

```cpp
strong_ordering operator<=>(const error_category& rhs) const noexcept;
```

**2** *Returns:* `compare_three_way()(this, &rhs)`.

[*Note 1*: `compare_three_way` ([[comparisons.three.way]](comparisons.three.way.md)) provides a total ordering for pointers. — *end note*]
