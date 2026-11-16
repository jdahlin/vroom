# 29.11.14.30 Remove [fs.op.remove]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.29 Relative \[fs.op.relative\]](fs.op.relative.md) · next: [29.11.14.31 Remove all \[fs.op.remove.all\]](fs.op.remove.all.md)

```cpp
bool remove(const path& p);
bool remove(const path& p, error_code& ec) noexcept;
```

**1** *Effects:* If `exists(symlink_status(p, ec))`, the file `p` is removed as if by POSIX `remove()`. [*Note 1*: A symbolic link is itself removed, rather than the file it resolves to. — *end note*]

**2** *Postconditions:* `exists(symlink_status(p))` is `false`.

**3** *Returns:* `false` if `p` did not exist, otherwise `true`. The signature with argument `ec` returns `false` if an error occurs.

**4** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
