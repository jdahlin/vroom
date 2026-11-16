# 29.5.6.1 `fmtflags` manipulators [fmtflags.manip]

[Contents](../README.md) · up: [29.5.6 `ios_base` manipulators \[std.ios.manip\]](std.ios.manip.md) · prev: [29.5.6 `ios_base` manipulators \[std.ios.manip\]](std.ios.manip.md) · next: [29.5.6.2 `adjustfield` manipulators \[adjustfield.manip\]](adjustfield.manip.md)

**1** Each function specified in this subclause is a designated addressable function ([[namespace.std]](namespace.std.md)).

```cpp
ios_base& boolalpha(ios_base& str);
```

**2** *Effects:* Calls `str.setf(ios_base::boolalpha)`.

**3** *Returns:* `str`.

```cpp
ios_base& noboolalpha(ios_base& str);
```

**4** *Effects:* Calls `str.unsetf(ios_base::boolalpha)`.

**5** *Returns:* `str`.

```cpp
ios_base& showbase(ios_base& str);
```

**6** *Effects:* Calls `str.setf(ios_base::showbase)`.

**7** *Returns:* `str`.

```cpp
ios_base& noshowbase(ios_base& str);
```

**8** *Effects:* Calls `str.unsetf(ios_base::showbase)`.

**9** *Returns:* `str`.

```cpp
ios_base& showpoint(ios_base& str);
```

**10** *Effects:* Calls `str.setf(ios_base::showpoint)`.

**11** *Returns:* `str`.

```cpp
ios_base& noshowpoint(ios_base& str);
```

**12** *Effects:* Calls `str.unsetf(ios_base::showpoint)`.

**13** *Returns:* `str`.

```cpp
ios_base& showpos(ios_base& str);
```

**14** *Effects:* Calls `str.setf(ios_base::showpos)`.

**15** *Returns:* `str`.

```cpp
ios_base& noshowpos(ios_base& str);
```

**16** *Effects:* Calls `str.unsetf(ios_base::showpos)`.

**17** *Returns:* `str`.

```cpp
ios_base& skipws(ios_base& str);
```

**18** *Effects:* Calls `str.setf(ios_base::skipws)`.

**19** *Returns:* `str`.

```cpp
ios_base& noskipws(ios_base& str);
```

**20** *Effects:* Calls `str.unsetf(ios_base::skipws)`.

**21** *Returns:* `str`.

```cpp
ios_base& uppercase(ios_base& str);
```

**22** *Effects:* Calls `str.setf(ios_base::uppercase)`.

**23** *Returns:* `str`.

```cpp
ios_base& nouppercase(ios_base& str);
```

**24** *Effects:* Calls `str.unsetf(ios_base::uppercase)`.

**25** *Returns:* `str`.

```cpp
ios_base& unitbuf(ios_base& str);
```

**26** *Effects:* Calls `str.setf(ios_base::unitbuf)`.

**27** *Returns:* `str`.

```cpp
ios_base& nounitbuf(ios_base& str);
```

**28** *Effects:* Calls `str.unsetf(ios_base::unitbuf)`.

**29** *Returns:* `str`.
