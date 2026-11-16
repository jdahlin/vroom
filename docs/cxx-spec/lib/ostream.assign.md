# 29.7.5.1.2 Assignment and swap [ostream.assign]

[Contents](../README.md) · up: [29.7.5.1 Class template `basic_ostream` \[ostream\]](ostream.md) · prev: [29.7.5.1.1 Constructors \[ostream.cons\]](ostream.cons.md) · next: [29.7.5.1.3 Class `basic_ostream::sentry` \[ostream.sentry\]](ostream.sentry.md)

```cpp
basic_ostream& operator=(basic_ostream&& rhs);
```

**1** *Effects:* Equivalent to: `swap(rhs)`.

**2** *Returns:* `*this`.

```cpp
void swap(basic_ostream& rhs);
```

**3** *Effects:* Calls `basic_ios<charT, traits>::swap(rhs)`.
