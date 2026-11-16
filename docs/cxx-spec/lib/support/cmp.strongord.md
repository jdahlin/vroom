# 17.11.2.4 Class `strong_ordering` [cmp.strongord]

[Contents](../../README.md) · up: [17.11.2 Comparison category types \[cmp.categories\]](cmp.categories.md) · prev: [17.11.2.3 Class `weak_ordering` \[cmp.weakord\]](cmp.weakord.md) · next: [17.11.3 Class template `common_comparison_category` \[cmp.common\]](cmp.common.md)

**1** The `strong_ordering` type is typically used as the result type of a three-way comparison operator ([[expr.spaceship]](../../expr/expr.spaceship.md)) that (a) admits all of the six two-way comparison operators ([[expr.rel]](../../expr/expr.rel.md), [[expr.eq]](../../expr/expr.eq.md)), and (b) does imply substitutability.

```cpp
namespace std {
  class strong_ordering {
    int value;  // exposition only

    // exposition-only constructors
    constexpr explicit strong_ordering(eq v) noexcept : value(int(v)) {}    // exposition only
    constexpr explicit strong_ordering(ord v) noexcept : value(int(v)) {}   // exposition only

  public:
    // valid values
    static const strong_ordering less;
    static const strong_ordering equal;
    static const strong_ordering equivalent;
    static const strong_ordering greater;

    // conversions
    constexpr operator partial_ordering() const noexcept;
    constexpr operator weak_ordering() const noexcept;

    // comparisons
    friend constexpr bool operator==(strong_ordering v, unspecified) noexcept;
    friend constexpr bool operator==(strong_ordering v, strong_ordering w) noexcept = default;
    friend constexpr bool operator< (strong_ordering v, unspecified) noexcept;
    friend constexpr bool operator> (strong_ordering v, unspecified) noexcept;
    friend constexpr bool operator<=(strong_ordering v, unspecified) noexcept;
    friend constexpr bool operator>=(strong_ordering v, unspecified) noexcept;
    friend constexpr bool operator< (unspecified, strong_ordering v) noexcept;
    friend constexpr bool operator> (unspecified, strong_ordering v) noexcept;
    friend constexpr bool operator<=(unspecified, strong_ordering v) noexcept;
    friend constexpr bool operator>=(unspecified, strong_ordering v) noexcept;
    friend constexpr strong_ordering operator<=>(strong_ordering v, unspecified) noexcept;
    friend constexpr strong_ordering operator<=>(unspecified, strong_ordering v) noexcept;
  };

  // valid values' definitions
  inline constexpr strong_ordering strong_ordering::less(ord::less);
  inline constexpr strong_ordering strong_ordering::equal(eq::equal);
  inline constexpr strong_ordering strong_ordering::equivalent(eq::equivalent);
  inline constexpr strong_ordering strong_ordering::greater(ord::greater);
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
constexpr operator weak_ordering() const noexcept;
```

**3** *Returns:*

```cpp
value == 0 ? weak_ordering::equivalent :
value < 0  ? weak_ordering::less :
             weak_ordering::greater
```

```cpp
constexpr bool operator==(strong_ordering v, unspecified) noexcept;
constexpr bool operator< (strong_ordering v, unspecified) noexcept;
constexpr bool operator> (strong_ordering v, unspecified) noexcept;
constexpr bool operator<=(strong_ordering v, unspecified) noexcept;
constexpr bool operator>=(strong_ordering v, unspecified) noexcept;
```

**4** *Returns:* `v.value @ 0` for `operator@`.

```cpp
constexpr bool operator< (unspecified, strong_ordering v) noexcept;
constexpr bool operator> (unspecified, strong_ordering v) noexcept;
constexpr bool operator<=(unspecified, strong_ordering v) noexcept;
constexpr bool operator>=(unspecified, strong_ordering v) noexcept;
```

**5** *Returns:* `0 @ v.value` for `operator@`.

```cpp
constexpr strong_ordering operator<=>(strong_ordering v, unspecified) noexcept;
```

**6** *Returns:* `v`.

```cpp
constexpr strong_ordering operator<=>(unspecified, strong_ordering v) noexcept;
```

**7** *Returns:* `v < 0 ? strong_ordering::greater: v > 0 ? strong_ordering::less: v`.
