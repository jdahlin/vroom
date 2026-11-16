# 29.5.6.2 `adjustfield` manipulators [adjustfield.manip]

[Contents](../README.md) · up: [29.5.6 `ios_base` manipulators \[std.ios.manip\]](std.ios.manip.md) · prev: [29.5.6.1 `fmtflags` manipulators \[fmtflags.manip\]](fmtflags.manip.md) · next: [29.5.6.3 `basefield` manipulators \[basefield.manip\]](basefield.manip.md)

**1** Each function specified in this subclause is a designated addressable function ([[namespace.std]](namespace.std.md)).

```cpp
ios_base& internal(ios_base& str);
```

**2** *Effects:* Calls `str.setf(ios_base::internal, ios_base::adjustfield)`.

**3** *Returns:* `str`.

```cpp
ios_base& left(ios_base& str);
```

**4** *Effects:* Calls `str.setf(ios_base::left, ios_base::adjustfield)`.

**5** *Returns:* `str`.

```cpp
ios_base& right(ios_base& str);
```

**6** *Effects:* Calls `str.setf(ios_base::right, ios_base::adjustfield)`.

**7** *Returns:* `str`.
