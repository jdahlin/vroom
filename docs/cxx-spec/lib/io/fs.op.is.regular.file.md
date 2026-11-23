# 29.11.14.22 Is regular file [fs.op.is.regular.file]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.21 Is other \[fs.op.is.other\]](fs.op.is.other.md) · next: [29.11.14.23 Is socket \[fs.op.is.socket\]](fs.op.is.socket.md)

```cpp
bool is_regular_file(file_status s) noexcept;
```

**1** *Returns:* `s.type() == file_type::regular`.

```cpp
bool is_regular_file(const path& p);
```

**2** *Returns:* `is_regular_file(status(p))`.

**3** *Throws:* `filesystem_error` if `status(p)` would throw `filesystem_error`.

```cpp
bool is_regular_file(const path& p, error_code& ec) noexcept;
```

**4** *Effects:* Sets `ec` as if by `status(p, ec)`. [*Note 1*: `file_type::none`, `file_type::not_found` and `file_type::unknown` cases set `ec` to error values. To distinguish between cases, call the `status` function directly. — *end note*]

**5** *Returns:* `is_regular_file(status(p, ec))`. Returns `false` if an error occurs.
