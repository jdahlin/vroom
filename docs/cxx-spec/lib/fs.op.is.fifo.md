# 29.11.14.20 Is fifo [fs.op.is.fifo]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.19 Is empty \[fs.op.is.empty\]](fs.op.is.empty.md) · next: [29.11.14.21 Is other \[fs.op.is.other\]](fs.op.is.other.md)

```cpp
bool is_fifo(file_status s) noexcept;
```

**1** *Returns:* `s.type() == file_type::fifo`.

```cpp
bool is_fifo(const path& p);
bool is_fifo(const path& p, error_code& ec) noexcept;
```

**2** *Returns:* `is_fifo(status(p))` or `is_fifo(status(p, ec))`, respectively. The signature with argument `ec` returns `false` if an error occurs.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
