# 29.11.14.8 Create directory symlink [fs.op.create.dir.symlk]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.7 Create directory \[fs.op.create.directory\]](fs.op.create.directory.md) · next: [29.11.14.9 Create hard link \[fs.op.create.hard.lk\]](fs.op.create.hard.lk.md)

```cpp
void create_directory_symlink(const path& to, const path& new_symlink);
void create_directory_symlink(const path& to, const path& new_symlink,
                              error_code& ec) noexcept;
```

**1** *Effects:* Establishes the postcondition, as if by POSIX `symlink()`.

**2** *Postconditions:* `new_symlink` resolves to a symbolic link file that contains an unspecified representation of `to`.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**4** [*Note 1*: Some operating systems require symlink creation to identify that the link is to a directory. Portable code should use `create_directory_symlink()` to create directory symlinks rather than `create_symlink()` — *end note*]

**5** [*Note 2*: Some operating systems do not support symbolic links at all or support them only for regular files. Some file systems (such as the FAT file system) do not support symbolic links regardless of the operating system. — *end note*]
