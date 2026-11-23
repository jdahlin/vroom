# 27.8.12.3 Non-member functions [time.cal.mwdlast.nonmembers]

[Contents](../../README.md) · up: [27.8.12 Class `month_weekday_last` \[time.cal.mwdlast\]](time.cal.mwdlast.md) · prev: [27.8.12.2 Member functions \[time.cal.mwdlast.members\]](time.cal.mwdlast.members.md) · next: [27.8.13 Class `year_month` \[time.cal.ym\]](time.cal.ym.md)

```cpp
constexpr bool operator==(const month_weekday_last& x, const month_weekday_last& y) noexcept;
```

**1** *Returns:* `x.month() == y.month() && x.weekday_last() == y.weekday_last()`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const month_weekday_last& mwdl);
```

**2** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{}/{}"),
                    mwdl.month(), mwdl.weekday_last());
```
