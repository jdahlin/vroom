# 27.8.8.3 Non-member functions [time.cal.wdlast.nonmembers]

[Contents](../README.md) · up: [27.8.8 Class `weekday_last` \[time.cal.wdlast\]](time.cal.wdlast.md) · prev: [27.8.8.2 Member functions \[time.cal.wdlast.members\]](time.cal.wdlast.members.md) · next: [27.8.9 Class `month_day` \[time.cal.md\]](time.cal.md.md)

```cpp
constexpr bool operator==(const weekday_last& x, const weekday_last& y) noexcept;
```

**1** *Returns:* `x.weekday() == y.weekday()`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const weekday_last& wdl);
```

**2** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{}[last]"), wdl.weekday());
```
