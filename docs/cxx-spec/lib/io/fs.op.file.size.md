# 29.11.14.14 File size [fs.op.file.size]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.13 Exists \[fs.op.exists\]](fs.op.exists.md) · next: [29.11.14.15 Hard link count \[fs.op.hard.lk.ct\]](fs.op.hard.lk.ct.md)

```cpp
uintmax_t file_size(const path& p);
uintmax_t file_size(const path& p, error_code& ec) noexcept;
```

**1** *Effects:* If `exists(p)` is `false`, an error is reported ([[fs.err.report]](fs.err.report.md)).

**2** *Returns:*

- If `is_regular_file(p)`, the size in bytes of the file `p` resolves to, determined as if by the value of the POSIX `stat` class member `st_size` obtained as if by POSIX `stat()`.
- Otherwise, the result is implementation-defined.

The signature with argument `ec` returns `static_cast<uintmax_t>(-1)` if an error occurs.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
