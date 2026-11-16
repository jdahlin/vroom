# 29.5.6.4 `floatfield` manipulators [floatfield.manip]

[Contents](../README.md) · up: [29.5.6 `ios_base` manipulators \[std.ios.manip\]](std.ios.manip.md) · prev: [29.5.6.3 `basefield` manipulators \[basefield.manip\]](basefield.manip.md) · next: [29.5.7 Error reporting \[error.reporting\]](error.reporting.md)

**1** Each function specified in this subclause is a designated addressable function ([[namespace.std]](namespace.std.md)).

```cpp
ios_base& fixed(ios_base& str);
```

**2** *Effects:* Calls `str.setf(ios_base::fixed, ios_base::floatfield)`.

**3** *Returns:* `str`.

```cpp
ios_base& scientific(ios_base& str);
```

**4** *Effects:* Calls `str.setf(ios_base::scientific, ios_base::floatfield)`.

**5** *Returns:* `str`.

```cpp
ios_base& hexfloat(ios_base& str);
```

**6** *Effects:* Calls `str.setf(ios_base::fixed | ios_base::scientific, ios_base::floatfield)`.

**7** *Returns:* `str`.

**8** [*Note 1*: The more obvious use of `ios_base::hex` to specify hexadecimal floating-point format would change the meaning of existing well-defined programs. C++ 2003 gives no meaning to the combination of `fixed` and `scientific`. — *end note*]

```cpp
ios_base& defaultfloat(ios_base& str);
```

**9** *Effects:* Calls `str.unsetf(ios_base::floatfield)`.

**10** *Returns:* `str`.
