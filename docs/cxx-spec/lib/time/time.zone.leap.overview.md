# 27.11.8.1 Overview [time.zone.leap.overview]

[Contents](../../README.md) · up: [27.11.8 Class `leap_second` \[time.zone.leap\]](time.zone.leap.md) · prev: [27.11.8 Class `leap_second` \[time.zone.leap\]](time.zone.leap.md) · next: [27.11.8.2 Member functions \[time.zone.leap.members\]](time.zone.leap.members.md)

```cpp
namespace std::chrono {
  class leap_second {
  public:
    leap_second(const leap_second&)            = default;
    leap_second& operator=(const leap_second&) = default;

    // unspecified additional constructors

    constexpr sys_seconds date() const noexcept;
    constexpr seconds value() const noexcept;
  };
}
```

**1** Objects of type `leap_second` representing the date and value of the leap second insertions are constructed and stored in the time zone database when initialized.

**2** [*Example 1*:

```cpp
for (auto& l : get_tzdb().leap_seconds)
  if (l <= 2018y/March/17d)
    cout << l.date() << ": " << l.value() << '\n';
```

Produces the output:

```text
1972-07-01 00:00:00: 1s
1973-01-01 00:00:00: 1s
1974-01-01 00:00:00: 1s
1975-01-01 00:00:00: 1s
1976-01-01 00:00:00: 1s
1977-01-01 00:00:00: 1s
1978-01-01 00:00:00: 1s
1979-01-01 00:00:00: 1s
1980-01-01 00:00:00: 1s
1981-07-01 00:00:00: 1s
1982-07-01 00:00:00: 1s
1983-07-01 00:00:00: 1s
1985-07-01 00:00:00: 1s
1988-01-01 00:00:00: 1s
1990-01-01 00:00:00: 1s
1991-01-01 00:00:00: 1s
1992-07-01 00:00:00: 1s
1993-07-01 00:00:00: 1s
1994-07-01 00:00:00: 1s
1996-01-01 00:00:00: 1s
1997-07-01 00:00:00: 1s
1999-01-01 00:00:00: 1s
2006-01-01 00:00:00: 1s
2009-01-01 00:00:00: 1s
2012-07-01 00:00:00: 1s
2015-07-01 00:00:00: 1s
2017-01-01 00:00:00: 1s
```

— *end example*]
