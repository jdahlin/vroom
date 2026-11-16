# 29.11.14.2 Canonical [fs.op.canonical]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.1 Absolute \[fs.op.absolute\]](fs.op.absolute.md) · next: [29.11.14.3 Copy \[fs.op.copy\]](fs.op.copy.md)

```cpp
path canonical(const path& p);
path canonical(const path& p, error_code& ec);
```

**1** *Effects:* Converts `p` to an absolute path that has no symbolic link, dot, or dot-dot elements in its pathname in the generic format.

**2** *Returns:* A path that refers to the same file system object as `absolute(p)`. The signature with argument `ec` returns `path()` if an error occurs.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**4** *Remarks:* `!exists(p)` is an error.
