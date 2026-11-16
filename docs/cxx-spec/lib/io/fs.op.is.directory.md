# 29.11.14.18 Is directory [fs.op.is.directory]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.17 Is character file \[fs.op.is.char.file\]](fs.op.is.char.file.md) · next: [29.11.14.19 Is empty \[fs.op.is.empty\]](fs.op.is.empty.md)

```cpp
bool is_directory(file_status s) noexcept;
```

**1** *Returns:* `s.type() == file_type::directory`.

```cpp
bool is_directory(const path& p);
bool is_directory(const path& p, error_code& ec) noexcept;
```

**2** *Returns:* `is_directory(status(p))` or `is_directory(status(p, ec))`, respectively. The signature with argument `ec` returns `false` if an error occurs.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
