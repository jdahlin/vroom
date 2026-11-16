# 29.11.14.17 Is character file [fs.op.is.char.file]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.16 Is block file \[fs.op.is.block.file\]](fs.op.is.block.file.md) · next: [29.11.14.18 Is directory \[fs.op.is.directory\]](fs.op.is.directory.md)

```cpp
bool is_character_file(file_status s) noexcept;
```

**1** *Returns:* `s.type() == file_type::character`.

```cpp
bool is_character_file(const path& p);
bool is_character_file(const path& p, error_code& ec) noexcept;
```

**2** *Returns:* `is_character_file(status(p))` or `is_character_file(status(p, ec))`, respectively.<br>The signature with argument `ec` returns `false` if an error occurs.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
