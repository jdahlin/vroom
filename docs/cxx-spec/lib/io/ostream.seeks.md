# 29.7.5.1.4 Seek members [ostream.seeks]

[Contents](../../README.md) · up: [29.7.5.1 Class template `basic_ostream` \[ostream\]](ostream.md) · prev: [29.7.5.1.3 Class `basic_ostream::sentry` \[ostream.sentry\]](ostream.sentry.md) · next: [29.7.5.2 Formatted output functions \[ostream.formatted\]](ostream.formatted.md)

**1** Each seek member function begins execution by constructing an object of class `sentry`. It returns by destroying the `sentry` object.

```cpp
pos_type tellp();
```

**2** *Returns:* If `fail() != false`, returns `pos_type(-1)` to indicate failure. Otherwise, returns `rdbuf()->pubseekoff(0, cur, out)`.

```cpp
basic_ostream<charT, traits>& seekp(pos_type pos);
```

**3** *Effects:* If `fail() != true`, executes `rdbuf()->pubseekpos(pos, ios_base::out)`. In case of failure, the function calls `setstate(failbit)` (which may throw `ios_base::failure`).

**4** *Returns:* `*this`.

```cpp
basic_ostream<charT, traits>& seekp(off_type off, ios_base::seekdir dir);
```

**5** *Effects:* If `fail() != true`, executes `rdbuf()->pubseekoff(off, dir, ios_base::out)`. In case of failure, the function calls `setstate(failbit)` (which may throw `ios_base::failure`).

**6** *Returns:* `*this`.
