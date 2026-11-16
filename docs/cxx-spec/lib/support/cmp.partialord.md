# 17.11.2.2 Class `partial_ordering` [cmp.partialord]

[Contents](../../README.md) · up: [17.11.2 Comparison category types \[cmp.categories\]](cmp.categories.md) · prev: [17.11.2.1 Preamble \[cmp.categories.pre\]](cmp.categories.pre.md) · next: [17.11.2.3 Class `weak_ordering` \[cmp.weakord\]](cmp.weakord.md)

**1** The `partial_ordering` type is typically used as the result type of a three-way comparison operator ([[expr.spaceship]](../../expr/expr.spaceship.md)) that (a) admits all of the six two-way comparison operators ([[expr.rel]](../../expr/expr.rel.md), [[expr.eq]](../../expr/expr.eq.md)), (b) does not imply substitutability, and (c) permits two values to be incomparable.[^1]

```cpp
namespace std {
  class partial_ordering {
    int value;          // exposition only
    bool is_ordered;    // exposition only

    // exposition-only constructors
    constexpr explicit
      partial_ordering(eq v) noexcept : value(int(v)), is_ordered(true) {}      // exposition only
    constexpr explicit
      partial_ordering(ord v) noexcept : value(int(v)), is_ordered(true) {}     // exposition only
    constexpr explicit
      partial_ordering(ncmp v) noexcept : value(int(v)), is_ordered(false) {}   // exposition only

  public:
    // valid values
    static const partial_ordering less;
    static const partial_ordering equivalent;
    static const partial_ordering greater;
    static const partial_ordering unordered;

    // comparisons
    friend constexpr bool operator==(partial_ordering v, unspecified) noexcept;
    friend constexpr bool operator==(partial_ordering v, partial_ordering w) noexcept = default;
    friend constexpr bool operator< (partial_ordering v, unspecified) noexcept;
    friend constexpr bool operator> (partial_ordering v, unspecified) noexcept;
    friend constexpr bool operator<=(partial_ordering v, unspecified) noexcept;
    friend constexpr bool operator>=(partial_ordering v, unspecified) noexcept;
    friend constexpr bool operator< (unspecified, partial_ordering v) noexcept;
    friend constexpr bool operator> (unspecified, partial_ordering v) noexcept;
    friend constexpr bool operator<=(unspecified, partial_ordering v) noexcept;
    friend constexpr bool operator>=(unspecified, partial_ordering v) noexcept;
    friend constexpr partial_ordering operator<=>(partial_ordering v, unspecified) noexcept;
    friend constexpr partial_ordering operator<=>(unspecified, partial_ordering v) noexcept;
  };

  // valid values' definitions
  inline constexpr partial_ordering partial_ordering::less(ord::less);
  inline constexpr partial_ordering partial_ordering::equivalent(eq::equivalent);
  inline constexpr partial_ordering partial_ordering::greater(ord::greater);
  inline constexpr partial_ordering partial_ordering::unordered(ncmp::unordered);
}
```

```cpp
constexpr bool operator==(partial_ordering v, unspecified) noexcept;
constexpr bool operator< (partial_ordering v, unspecified) noexcept;
constexpr bool operator> (partial_ordering v, unspecified) noexcept;
constexpr bool operator<=(partial_ordering v, unspecified) noexcept;
constexpr bool operator>=(partial_ordering v, unspecified) noexcept;
```

**2** *Returns:* For `operator@`, `v.is_ordered && v.value @ 0`.

```cpp
constexpr bool operator< (unspecified, partial_ordering v) noexcept;
constexpr bool operator> (unspecified, partial_ordering v) noexcept;
constexpr bool operator<=(unspecified, partial_ordering v) noexcept;
constexpr bool operator>=(unspecified, partial_ordering v) noexcept;
```

**3** *Returns:* For `operator@`, `v.is_ordered && 0 @ v.value`.

```cpp
constexpr partial_ordering operator<=>(partial_ordering v, unspecified) noexcept;
```

**4** *Returns:* `v`.

```cpp
constexpr partial_ordering operator<=>(unspecified, partial_ordering v) noexcept;
```

**5** *Returns:* `v < 0 ? partial_ordering::greater: v > 0 ? partial_ordering::less: v`.

[^1]: That is, `a < b`, `a == b`, and `a > b` might all be `false`.
