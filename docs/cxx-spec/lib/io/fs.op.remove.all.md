# 29.11.14.31 Remove all [fs.op.remove.all]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.30 Remove \[fs.op.remove\]](fs.op.remove.md) · next: [29.11.14.32 Rename \[fs.op.rename\]](fs.op.rename.md)

```cpp
uintmax_t remove_all(const path& p);
uintmax_t remove_all(const path& p, error_code& ec);
```

**1** *Effects:* Recursively deletes the contents of `p` if it exists, then deletes file `p` itself, as if by POSIX `remove()`. [*Note 1*: A symbolic link is itself removed, rather than the file it resolves to. — *end note*]

**2** *Postconditions:* `exists(symlink_status(p))` is `false`.

**3** *Returns:* The number of files removed. The signature with argument `ec` returns `static_cast< uintmax_t>(-1)` if an error occurs.

**4** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
