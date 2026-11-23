# 29.11.14.28 Read symlink [fs.op.read.symlink]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.27 Proximate \[fs.op.proximate\]](fs.op.proximate.md) · next: [29.11.14.29 Relative \[fs.op.relative\]](fs.op.relative.md)

```cpp
path read_symlink(const path& p);
path read_symlink(const path& p, error_code& ec);
```

**1** *Returns:* If `p` resolves to a symbolic link, a `path` object containing the contents of that symbolic link. The signature with argument `ec` returns `path()` if an error occurs.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md). [*Note 1*: It is an error if `p` does not resolve to a symbolic link. — *end note*]
