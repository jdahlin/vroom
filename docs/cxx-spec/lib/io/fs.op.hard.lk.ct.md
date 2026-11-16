# 29.11.14.15 Hard link count [fs.op.hard.lk.ct]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.14 File size \[fs.op.file.size\]](fs.op.file.size.md) · next: [29.11.14.16 Is block file \[fs.op.is.block.file\]](fs.op.is.block.file.md)

```cpp
uintmax_t hard_link_count(const path& p);
uintmax_t hard_link_count(const path& p, error_code& ec) noexcept;
```

**1** *Returns:* The number of hard links for `p`. The signature with argument `ec` returns `static_cast<uintmax_t>(-1)` if an error occurs.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
