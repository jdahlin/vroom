# 29.11.14.33 Resize file [fs.op.resize.file]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.32 Rename \[fs.op.rename\]](fs.op.rename.md) · next: [29.11.14.34 Space \[fs.op.space\]](fs.op.space.md)

```cpp
void resize_file(const path& p, uintmax_t new_size);
void resize_file(const path& p, uintmax_t new_size, error_code& ec) noexcept;
```

**1** *Effects:* Causes the size that would be returned by `file_size(p)` to be equal to `new_size`, as if by POSIX `truncate()`.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
