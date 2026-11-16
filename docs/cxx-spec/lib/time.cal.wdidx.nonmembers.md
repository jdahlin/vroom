# 27.8.7.3 Non-member functions [time.cal.wdidx.nonmembers]

[Contents](../README.md) · up: [27.8.7 Class `weekday_indexed` \[time.cal.wdidx\]](time.cal.wdidx.md) · prev: [27.8.7.2 Member functions \[time.cal.wdidx.members\]](time.cal.wdidx.members.md) · next: [27.8.8 Class `weekday_last` \[time.cal.wdlast\]](time.cal.wdlast.md)

```cpp
constexpr bool operator==(const weekday_indexed& x, const weekday_indexed& y) noexcept;
```

**1** *Returns:* `x.weekday() == y.weekday() && x.index() == y.index()`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const weekday_indexed& wdi);
```

**2** *Effects:* Equivalent to:

```cpp
auto i = wdi.index();
return os << (i >= 1 && i <= 5 ?
  format(os.getloc(), STATICALLY-WIDEN<charT>("{}[{}]"), wdi.weekday(), i) :
  format(os.getloc(), STATICALLY-WIDEN<charT>("{}[{} is not a valid index]"),
         wdi.weekday(), i));
```
