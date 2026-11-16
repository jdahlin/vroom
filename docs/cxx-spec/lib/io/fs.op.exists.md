# 29.11.14.13 Exists [fs.op.exists]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.12 Equivalent \[fs.op.equivalent\]](fs.op.equivalent.md) · next: [29.11.14.14 File size \[fs.op.file.size\]](fs.op.file.size.md)

```cpp
bool exists(file_status s) noexcept;
```

**1** *Returns:* `status_known(s) && s.type() != file_type::not_found`.

```cpp
bool exists(const path& p);
bool exists(const path& p, error_code& ec) noexcept;
```

**2** Let `s` be a `file_status`, determined as if by `status(p)` or `status(p, ec)`, respectively.

**3** *Effects:* The signature with argument `ec` calls `ec.clear()` if `status_known(s)`.

**4** *Returns:* `exists(s)`.

**5** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
