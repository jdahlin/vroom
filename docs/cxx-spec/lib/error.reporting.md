# 29.5.7 Error reporting [error.reporting]

[Contents](../README.md) · up: [29.5 Iostreams base classes \[iostreams.base\]](iostreams.base.md) · prev: [29.5.6.4 `floatfield` manipulators \[floatfield.manip\]](floatfield.manip.md) · next: [29.6 Stream buffers \[stream.buffers\]](stream.buffers.md)

```cpp
error_code make_error_code(io_errc e) noexcept;
```

**1** *Returns:* `error_code(static_cast<int>(e), iostream_category())`.

```cpp
error_condition make_error_condition(io_errc e) noexcept;
```

**2** *Returns:* `error_condition(static_cast<int>(e), iostream_category())`.

```cpp
const error_category& iostream_category() noexcept;
```

**3** *Returns:* A reference to an object of a type derived from class `error_category`.

**4** The object’s `default_error_condition` and `equivalent` virtual functions shall behave as specified for the class `error_category`. The object’s `name` virtual function shall return a pointer to the string `"iostream"`.
