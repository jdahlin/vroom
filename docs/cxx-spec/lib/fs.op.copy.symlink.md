# 29.11.14.5 Copy symlink [fs.op.copy.symlink]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.4 Copy file \[fs.op.copy.file\]](fs.op.copy.file.md) · next: [29.11.14.6 Create directories \[fs.op.create.directories\]](fs.op.create.directories.md)

```cpp
void copy_symlink(const path& existing_symlink, const path& new_symlink);
void copy_symlink(const path& existing_symlink, const path& new_symlink,
                  error_code& ec) noexcept;
```

**1** *Effects:* Equivalent to `function(read_symlink(existing_symlink), new_symlink)` or<br> `function(read_symlink(existing_symlink, ec), new_symlink, ec)`, respectively, where in each case `function` is `create_symlink` or `create_directory_symlink` as appropriate.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
