# 29.11.14.29 Relative [fs.op.relative]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.28 Read symlink \[fs.op.read.symlink\]](fs.op.read.symlink.md) · next: [29.11.14.30 Remove \[fs.op.remove\]](fs.op.remove.md)

```cpp
path relative(const path& p, error_code& ec);
```

**1** *Returns:* `relative(p, current_path(), ec)`.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
path relative(const path& p, const path& base = current_path());
path relative(const path& p, const path& base, error_code& ec);
```

**3** *Returns:* For the first form:

```cpp
weakly_canonical(p).lexically_relative(weakly_canonical(base));
```

For the second form:

```cpp
weakly_canonical(p, ec).lexically_relative(weakly_canonical(base, ec));
```

or `path()` at the first error occurrence, if any.

**4** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
