# 29.11.14.6 Create directories [fs.op.create.directories]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.5 Copy symlink \[fs.op.copy.symlink\]](fs.op.copy.symlink.md) · next: [29.11.14.7 Create directory \[fs.op.create.directory\]](fs.op.create.directory.md)

```cpp
bool create_directories(const path& p);
bool create_directories(const path& p, error_code& ec);
```

**1** *Effects:* Calls `create_directory()` for each element of `p` that does not exist.

**2** *Returns:* `true` if a new directory was created for the directory `p` resolves to, otherwise `false`.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**4** *Complexity:* 𝑂(*n*) where *n* is the number of elements of `p`.
