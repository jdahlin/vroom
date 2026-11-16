# 20.7.9 `monostate` relational operators [variant.monostate.relops]

[Contents](../README.md) · up: [20.7 Variants \[variant\]](variant.md) · prev: [20.7.8 Class `monostate` \[variant.monostate\]](variant.monostate.md) · next: [20.7.10 Specialized algorithms \[variant.specalg\]](variant.specalg.md)

```cpp
constexpr bool operator==(monostate, monostate) noexcept { return true; }
constexpr strong_ordering operator<=>(monostate, monostate) noexcept
{ return strong_ordering::equal; }
```

**1** [*Note 1*: `monostate` objects have only a single state; they thus always compare equal. — *end note*]
