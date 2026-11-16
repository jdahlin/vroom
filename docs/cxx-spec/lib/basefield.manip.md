# 29.5.6.3 `basefield` manipulators [basefield.manip]

[Contents](../README.md) · up: [29.5.6 `ios_base` manipulators \[std.ios.manip\]](std.ios.manip.md) · prev: [29.5.6.2 `adjustfield` manipulators \[adjustfield.manip\]](adjustfield.manip.md) · next: [29.5.6.4 `floatfield` manipulators \[floatfield.manip\]](floatfield.manip.md)

**1** Each function specified in this subclause is a designated addressable function ([[namespace.std]](namespace.std.md)).

```cpp
ios_base& dec(ios_base& str);
```

**2** *Effects:* Calls `str.setf(ios_base::dec, ios_base::basefield)`.

**3** *Returns:* `str`[^1].

```cpp
ios_base& hex(ios_base& str);
```

**4** *Effects:* Calls `str.setf(ios_base::hex, ios_base::basefield)`.

**5** *Returns:* `str`.

```cpp
ios_base& oct(ios_base& str);
```

**6** *Effects:* Calls `str.setf(ios_base::oct, ios_base::basefield)`.

**7** *Returns:* `str`.

[^1]: The function signature `dec(ios_base&)` can be called by the function signature `basic_ostream& stream::operator<<(ios_base& (*)(ios_base&))` to permit expressions of the form `cout << dec` to change the format flags stored in `cout`.
