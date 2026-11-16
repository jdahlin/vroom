# 17.11.2.3 Class `weak_ordering` [cmp.weakord]

[Contents](../README.md) · up: [17.11.2 Comparison category types \[cmp.categories\]](cmp.categories.md) · prev: [17.11.2.2 Class `partial_ordering` \[cmp.partialord\]](cmp.partialord.md) · next: [17.11.2.4 Class `strong_ordering` \[cmp.strongord\]](cmp.strongord.md)

**1** The `weak_ordering` type is typically used as the result type of a three-way comparison operator ([[expr.spaceship]](../expr.spaceship.md)) that (a) admits all of the six two-way comparison operators ([[expr.rel]](../expr.rel.md), [[expr.eq]](../expr.eq.md)), and (b) does not imply substitutability.

```cpp
namespace std {
  class weak_ordering {
    int value;  // exposition only

    // exposition-only constructors
    constexpr explicit weak_ordering(eq v) noexcept : value(int(v)) {}  // exposition only
    constexpr explicit weak_ordering(ord v) noexcept : value(int(v)) {} // exposition only

  public:
    // valid values
    static const weak_ordering less;
    static const weak_ordering equivalent;
    static const weak_ordering greater;

    // conversions
    constexpr operator partial_ordering() const noexcept;

    // comparisons
    friend constexpr bool operator==(weak_ordering v, unspecified) noexcept;
    friend constexpr bool operator==(weak_ordering v, weak_ordering w) noexcept = default;
    friend constexpr bool operator< (weak_ordering v, unspecified) noexcept;
    friend constexpr bool operator> (weak_ordering v, unspecified) noexcept;
    friend constexpr bool operator<=(weak_ordering v, unspecified) noexcept;
    friend constexpr bool operator>=(weak_ordering v, unspecified) noexcept;
    friend constexpr bool operator< (unspecified, weak_ordering v) noexcept;
    friend constexpr bool operator> (unspecified, weak_ordering v) noexcept;
    friend constexpr bool operator<=(unspecified, weak_ordering v) noexcept;
    friend constexpr bool operator>=(unspecified, weak_ordering v) noexcept;
    friend constexpr weak_ordering operator<=>(weak_ordering v, unspecified) noexcept;
    friend constexpr weak_ordering operator<=>(unspecified, weak_ordering v) noexcept;
  };

  // valid values' definitions
  inline constexpr weak_ordering weak_ordering::less(ord::less);
  inline constexpr weak_ordering weak_ordering::equivalent(eq::equivalent);
  inline constexpr weak_ordering weak_ordering::greater(ord::greater);
}
```

```cpp
constexpr operator partial_ordering() const noexcept;
```

**2** *Returns:*

```cpp
value == 0 ? partial_ordering::equivalent :
value < 0  ? partial_ordering::less :
             partial_ordering::greater
```

```cpp
constexpr bool operator==(weak_ordering v, unspecified) noexcept;
constexpr bool operator< (weak_ordering v, unspecified) noexcept;
constexpr bool operator> (weak_ordering v, unspecified) noexcept;
constexpr bool operator<=(weak_ordering v, unspecified) noexcept;
constexpr bool operator>=(weak_ordering v, unspecified) noexcept;
```

**3** *Returns:* `v.value @ 0` for `operator@`.

```cpp
constexpr bool operator< (unspecified, weak_ordering v) noexcept;
constexpr bool operator> (unspecified, weak_ordering v) noexcept;
constexpr bool operator<=(unspecified, weak_ordering v) noexcept;
constexpr bool operator>=(unspecified, weak_ordering v) noexcept;
```

**4** *Returns:* `0 @ v.value` for `operator@`.

```cpp
constexpr weak_ordering operator<=>(weak_ordering v, unspecified) noexcept;
```

**5** *Returns:* `v`.

```cpp
constexpr weak_ordering operator<=>(unspecified, weak_ordering v) noexcept;
```

**6** *Returns:* `v < 0 ? weak_ordering::greater: v > 0 ? weak_ordering::less: v`.
