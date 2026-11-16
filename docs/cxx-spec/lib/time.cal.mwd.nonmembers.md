# 27.8.11.3 Non-member functions [time.cal.mwd.nonmembers]

[Contents](../README.md) · up: [27.8.11 Class `month_weekday` \[time.cal.mwd\]](time.cal.mwd.md) · prev: [27.8.11.2 Member functions \[time.cal.mwd.members\]](time.cal.mwd.members.md) · next: [27.8.12 Class `month_weekday_last` \[time.cal.mwdlast\]](time.cal.mwdlast.md)

```cpp
constexpr bool operator==(const month_weekday& x, const month_weekday& y) noexcept;
```

**1** *Returns:* `x.month() == y.month() && x.weekday_indexed() == y.weekday_indexed()`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const month_weekday& mwd);
```

**2** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{}/{}"),
                    mwd.month(), mwd.weekday_indexed());
```
