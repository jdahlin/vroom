# 29.11.14.12 Equivalent [fs.op.equivalent]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.11 Current path \[fs.op.current.path\]](fs.op.current.path.md) · next: [29.11.14.13 Exists \[fs.op.exists\]](fs.op.exists.md)

```cpp
bool equivalent(const path& p1, const path& p2);
bool equivalent(const path& p1, const path& p2, error_code& ec) noexcept;
```

**1** *Returns:* `true`, if `p1` and `p2` resolve to the same file system entity, otherwise `false`. The signature with argument `ec` returns `false` if an error occurs.

**2** Two paths are considered to resolve to the same file system entity if two candidate entities reside on the same device at the same location. [*Note 1*: On POSIX platforms, this is determined as if by the values of the POSIX `stat` class, obtained as if by `stat()` for the two paths, having equal `st_dev` values and equal `st_ino` values. — *end note*]

**3** *Remarks:* `!exists(p1) || !exists(p2)` is an error.

**4** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
