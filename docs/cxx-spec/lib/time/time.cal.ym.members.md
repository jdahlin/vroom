# 27.8.13.2 Member functions [time.cal.ym.members]

[Contents](../../README.md) · up: [27.8.13 Class `year_month` \[time.cal.ym\]](time.cal.ym.md) · prev: [27.8.13.1 Overview \[time.cal.ym.overview\]](time.cal.ym.overview.md) · next: [27.8.13.3 Non-member functions \[time.cal.ym.nonmembers\]](time.cal.ym.nonmembers.md)

```cpp
constexpr year_month(const chrono::year& y, const chrono::month& m) noexcept;
```

**1** *Effects:* Initializes `y_` with `y`, and `m_` with `m`.

```cpp
constexpr chrono::year year() const noexcept;
```

**2** *Returns:* `y_`.

```cpp
constexpr chrono::month month() const noexcept;
```

**3** *Returns:* `m_`.

```cpp
constexpr year_month& operator+=(const months& dm) noexcept;
```

**4** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../../over/over.ics.rank.md)).

**5** *Effects:* `*this = *this + dm`.

**6** *Returns:* `*this`.

```cpp
constexpr year_month& operator-=(const months& dm) noexcept;
```

**7** *Constraints:* If the argument supplied by the caller for the `months` parameter is convertible to `years`, its implicit conversion sequence to `years` is worse than its implicit conversion sequence to `months` ([[over.ics.rank]](../../over/over.ics.rank.md)).

**8** *Effects:* `*this = *this - dm`.

**9** *Returns:* `*this`.

```cpp
constexpr year_month& operator+=(const years& dy) noexcept;
```

**10** *Effects:* `*this = *this + dy`.

**11** *Returns:* `*this`.

```cpp
constexpr year_month& operator-=(const years& dy) noexcept;
```

**12** *Effects:* `*this = *this - dy`.

**13** *Returns:* `*this`.

```cpp
constexpr bool ok() const noexcept;
```

**14** *Returns:* `y_.ok() && m_.ok()`.
