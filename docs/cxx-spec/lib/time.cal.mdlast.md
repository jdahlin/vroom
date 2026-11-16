# 27.8.10 Class `month_day_last` [time.cal.mdlast]

[Contents](../README.md) · up: [27.8 The civil calendar \[time.cal\]](time.cal.md) · prev: [27.8.9.3 Non-member functions \[time.cal.md.nonmembers\]](time.cal.md.nonmembers.md) · next: [27.8.11 Class `month_weekday` \[time.cal.mwd\]](time.cal.mwd.md)

```cpp
namespace std::chrono {
  class month_day_last {
    chrono::month m_;                   // exposition only

  public:
    constexpr explicit month_day_last(const chrono::month& m) noexcept;

    constexpr chrono::month month() const noexcept;
    constexpr bool ok() const noexcept;
  };
}
```

**1** `month_day_last` represents the last day of a month.

**2** [*Note 1*: A `month_day_last` object can be constructed using the expression `m/last` or `last/m`, where `m` is an expression of type `month`. — *end note*] [*Example 1*:

```cpp
constexpr auto mdl = February/last;     // mdl is the last day of February of an as yet unspecified year
static_assert(mdl.month() == February);
```

— *end example*]

**3** `month_day_last` is a trivially copyable and standard-layout class type.

```cpp
constexpr explicit month_day_last(const chrono::month& m) noexcept;
```

**4** *Effects:* Initializes `m_` with `m`.

```cpp
constexpr month month() const noexcept;
```

**5** *Returns:* `m_`.

```cpp
constexpr bool ok() const noexcept;
```

**6** *Returns:* `m_.ok()`.

```cpp
constexpr bool operator==(const month_day_last& x, const month_day_last& y) noexcept;
```

**7** *Returns:* `x.month() == y.month()`.

```cpp
constexpr strong_ordering operator<=>(const month_day_last& x, const month_day_last& y) noexcept;
```

**8** *Returns:* `x.month() <=> y.month()`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const month_day_last& mdl);
```

**9** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{}/last"), mdl.month());
```
