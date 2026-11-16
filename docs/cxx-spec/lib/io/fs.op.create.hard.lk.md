# 29.11.14.9 Create hard link [fs.op.create.hard.lk]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.8 Create directory symlink \[fs.op.create.dir.symlk\]](fs.op.create.dir.symlk.md) · next: [29.11.14.10 Create symlink \[fs.op.create.symlink\]](fs.op.create.symlink.md)

```cpp
void create_hard_link(const path& to, const path& new_hard_link);
void create_hard_link(const path& to, const path& new_hard_link,
                                      error_code& ec) noexcept;
```

**1** *Effects:* Establishes the postcondition, as if by POSIX `link()`.

**2** *Postconditions:*

- `exists(to) && exists(new_hard_link) && equivalent(to, new_hard_link)`
- The contents of the file or directory `to` resolves to are unchanged.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**4** [*Note 1*: Some operating systems do not support hard links at all or support them only for regular files. Some file systems (such as the FAT file system) do not support hard links regardless of the operating system. Some file systems limit the number of links per file. — *end note*]
