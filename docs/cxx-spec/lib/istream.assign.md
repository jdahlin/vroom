# 29.7.4.1.2 Assignment and swap [istream.assign]

[Contents](../README.md) · up: [29.7.4.1 Class template `basic_istream` \[istream\]](istream.md) · prev: [29.7.4.1.1 Constructors \[istream.cons\]](istream.cons.md) · next: [29.7.4.1.3 Class `basic_istream::sentry` \[istream.sentry\]](istream.sentry.md)

```cpp
basic_istream& operator=(basic_istream&& rhs);
```

**1** *Effects:* Equivalent to: `swap(rhs)`.

**2** *Returns:* `*this`.

```cpp
void swap(basic_istream& rhs);
```

**3** *Effects:* Calls `basic_ios<charT, traits>::swap(rhs)`. Exchanges the values returned by `gcount()` and `rhs.gcount()`.
