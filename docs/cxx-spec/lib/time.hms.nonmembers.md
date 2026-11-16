# 27.9.3 Non-members [time.hms.nonmembers]

[Contents](../README.md) · up: [27.9 Class template `hh_mm_ss` \[time.hms\]](time.hms.md) · prev: [27.9.2 Members \[time.hms.members\]](time.hms.members.md) · next: [27.10 12/24 hours functions \[time.12\]](time.12.md)

```cpp
template<class charT, class traits, class Duration>
basic_ostream<charT, traits>&
operator<<(basic_ostream<charT, traits>& os, const hh_mm_ss<Duration>& hms);
```

**1** *Effects:* Equivalent to:

```cpp
return os << format(os.getloc(), STATICALLY-WIDEN<charT>("{:%T}"), hms);
```

**2** [*Example 1*:

```cpp
for (auto ms : {-4083007ms, 4083007ms, 65745123ms}) {
  hh_mm_ss hms{ms};
  cout << hms << '\n';
}
cout << hh_mm_ss{65745s} << '\n';
```

Produces the output (assuming the "C" locale):

```cpp
-01:08:03.007
01:08:03.007
18:15:45.123
18:15:45
```

— *end example*]
