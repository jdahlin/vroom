# 27.5.10 I/O [time.duration.io]

[Contents](../README.md) · up: [27.5 Class template `duration` \[time.duration\]](time.duration.md) · prev: [27.5.9 Algorithms \[time.duration.alg\]](time.duration.alg.md) · next: [27.6 Class template `time_point` \[time.point\]](time.point.md)

```cpp
template<class charT, class traits, class Rep, class Period>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const duration<Rep, Period>& d);
```

**1** *Effects:* Inserts the duration `d` onto the stream `os` as if it were implemented as follows:

```cpp
basic_ostringstream<charT, traits> s;
s.flags(os.flags());
s.imbue(os.getloc());
s.precision(os.precision());
s << d.count() << units-suffix;
return os << s.str();
```

where `units-suffix` depends on the type `Period::type` as follows:

- If `Period::type` is `atto`, `units-suffix` is `"as"`.
- Otherwise, if `Period::type` is `femto`, `units-suffix` is `"fs"`.
- Otherwise, if `Period::type` is `pico`, `units-suffix` is `"ps"`.
- Otherwise, if `Period::type` is `nano`, `units-suffix` is `"ns"`.
- Otherwise, if `Period::type` is `micro`, it is implementation-defined whether `units-suffix` is `"µs"` (`"\u00b5\u0073"`) or `"us"`.
- Otherwise, if `Period::type` is `milli`, `units-suffix` is `"ms"`.
- Otherwise, if `Period::type` is `centi`, `units-suffix` is `"cs"`.
- Otherwise, if `Period::type` is `deci`, `units-suffix` is `"ds"`.
- Otherwise, if `Period::type` is `ratio<1>`, `units-suffix` is `"s"`.
- Otherwise, if `Period::type` is `deca`, `units-suffix` is `"das"`.
- Otherwise, if `Period::type` is `hecto`, `units-suffix` is `"hs"`.
- Otherwise, if `Period::type` is `kilo`, `units-suffix` is `"ks"`.
- Otherwise, if `Period::type` is `mega`, `units-suffix` is `"Ms"`.
- Otherwise, if `Period::type` is `giga`, `units-suffix` is `"Gs"`.
- Otherwise, if `Period::type` is `tera`, `units-suffix` is `"Ts"`.
- Otherwise, if `Period::type` is `peta`, `units-suffix` is `"Ps"`.
- Otherwise, if `Period::type` is `exa`, `units-suffix` is `"Es"`.
- Otherwise, if `Period::type` is `ratio<60>`, `units-suffix` is `"min"`.
- Otherwise, if `Period::type` is `ratio<3600>`, `units-suffix` is `"h"`.
- Otherwise, if `Period::type` is `ratio<86400>`, `units-suffix` is `"d"`.
- Otherwise, if `Period::type::den == 1`, `units-suffix` is `"[num]s"`.
- Otherwise, `units-suffix` is `"[num/den]s"`.

In the list above, the use of `num` and `den` refer to the static data members of `Period::type`, which are converted to arrays of `charT` using a decimal conversion with no leading zeroes.

**2** *Returns:* `os`.

```cpp
template<class charT, class traits, class Rep, class Period, class Alloc = allocator<charT>>
  basic_istream<charT, traits>&
    from_stream(basic_istream<charT, traits>& is, const charT* fmt,
                duration<Rep, Period>& d,
                basic_string<charT, traits, Alloc>* abbrev = nullptr,
                minutes* offset = nullptr);
```

**3** *Effects:* Attempts to parse the input stream `is` into the duration `d` using the format flags given in the NTCTS `fmt` as specified in [[time.parse]](time.parse.md). If the parse parses everything specified by the parsing format flags without error, and yet none of the flags impacts a duration, `d` will be assigned a zero value. If `%Z` is used and successfully parsed, that value will be assigned to `*abbrev` if `abbrev` is non-null. If `%z` (or a modified variant) is used and successfully parsed, that value will be assigned to `*offset` if `offset` is non-null.

**4** *Returns:* `is`.
