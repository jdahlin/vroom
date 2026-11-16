# 29.11.14.21 Is other [fs.op.is.other]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.20 Is fifo \[fs.op.is.fifo\]](fs.op.is.fifo.md) · next: [29.11.14.22 Is regular file \[fs.op.is.regular.file\]](fs.op.is.regular.file.md)

```cpp
bool is_other(file_status s) noexcept;
```

**1** *Returns:* `exists(s) && !is_regular_file(s) && !is_directory(s) && !is_symlink(s)`.

```cpp
bool is_other(const path& p);
bool is_other(const path& p, error_code& ec) noexcept;
```

**2** *Returns:* `is_other(status(p))` or `is_other(status(p, ec))`, respectively. The signature with argument `ec` returns `false` if an error occurs.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
