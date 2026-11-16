# 29.11.14.23 Is socket [fs.op.is.socket]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.22 Is regular file \[fs.op.is.regular.file\]](fs.op.is.regular.file.md) · next: [29.11.14.24 Is symlink \[fs.op.is.symlink\]](fs.op.is.symlink.md)

```cpp
bool is_socket(file_status s) noexcept;
```

**1** *Returns:* `s.type() == file_type::socket`.

```cpp
bool is_socket(const path& p);
bool is_socket(const path& p, error_code& ec) noexcept;
```

**2** *Returns:* `is_socket(status(p))` or `is_socket(status(p, ec))`, respectively. The signature with argument `ec` returns `false` if an error occurs.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
