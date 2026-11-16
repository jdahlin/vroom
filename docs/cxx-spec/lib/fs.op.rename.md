# 29.11.14.32 Rename [fs.op.rename]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.31 Remove all \[fs.op.remove.all\]](fs.op.remove.all.md) · next: [29.11.14.33 Resize file \[fs.op.resize.file\]](fs.op.resize.file.md)

```cpp
void rename(const path& old_p, const path& new_p);
void rename(const path& old_p, const path& new_p, error_code& ec) noexcept;
```

**1** *Effects:* Renames `old_p` to `new_p`, as if by POSIX `rename()`.

[*Note 1*:

- If `old_p` and `new_p` resolve to the same existing file, no action is taken.

- Otherwise, the rename may include the following effects:

  - if `new_p` resolves to an existing non-directory file, `new_p` is removed; otherwise,
  - if `new_p` resolves to an existing directory, `new_p` is removed if empty on POSIX compliant operating systems but may be an error on other operating systems.

A symbolic link is itself renamed, rather than the file it resolves to. — *end note*]

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
