# 29.11.14.24 Is symlink [fs.op.is.symlink]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.23 Is socket \[fs.op.is.socket\]](fs.op.is.socket.md) · next: [29.11.14.25 Last write time \[fs.op.last.write.time\]](fs.op.last.write.time.md)

```cpp
bool is_symlink(file_status s) noexcept;
```

**1** *Returns:* `s.type() == file_type::symlink`.

```cpp
bool is_symlink(const path& p);
bool is_symlink(const path& p, error_code& ec) noexcept;
```

**2** *Returns:* `is_symlink(symlink_status(p))` or `is_symlink(symlink_status(p, ec))`, respectively. The signature with argument `ec` returns `false` if an error occurs.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
