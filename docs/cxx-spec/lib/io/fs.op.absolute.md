# 29.11.14.1 Absolute [fs.op.absolute]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · next: [29.11.14.2 Canonical \[fs.op.canonical\]](fs.op.canonical.md)

```cpp
path absolute(const path& p);
path absolute(const path& p, error_code& ec);
```

**1** *Effects:* Composes an absolute path referencing the same file system location as `p` according to the operating system ([[fs.conform.os]](fs.conform.os.md)).

**2** *Returns:* The composed path. The signature with argument `ec` returns `path()` if an error occurs.

**3** [*Note 1*: For the returned path, `rp`, `rp.is_absolute()` is `true` unless an error occurs. — *end note*]

**4** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**5** [*Note 2*: To resolve symlinks, or perform other sanitization which might require queries to secondary storage, such as hard disks, consider `canonical` ([[fs.op.canonical]](fs.op.canonical.md)). — *end note*]

**6** [*Note 3*: Implementations are strongly encouraged to not query secondary storage, and not consider `!exists(p)` an error. — *end note*]

**7** [*Example 1*: For POSIX-based operating systems, `absolute(p)` is simply `current_path()/p`. For Windows-based operating systems, `absolute` might have the same semantics as `GetFullPathNameW`. — *end example*]
