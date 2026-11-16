# 29.11.14.16 Is block file [fs.op.is.block.file]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.15 Hard link count \[fs.op.hard.lk.ct\]](fs.op.hard.lk.ct.md) · next: [29.11.14.17 Is character file \[fs.op.is.char.file\]](fs.op.is.char.file.md)

```cpp
bool is_block_file(file_status s) noexcept;
```

**1** *Returns:* `s.type() == file_type::block`.

```cpp
bool is_block_file(const path& p);
bool is_block_file(const path& p, error_code& ec) noexcept;
```

**2** *Returns:* `is_block_file(status(p))` or `is_block_file(status(p, ec))`, respectively. The signature with argument `ec` returns `false` if an error occurs.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
