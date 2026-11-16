# 29.11.14.7 Create directory [fs.op.create.directory]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.6 Create directories \[fs.op.create.directories\]](fs.op.create.directories.md) · next: [29.11.14.8 Create directory symlink \[fs.op.create.dir.symlk\]](fs.op.create.dir.symlk.md)

```cpp
bool create_directory(const path& p);
bool create_directory(const path& p, error_code& ec) noexcept;
```

**1** *Effects:* Creates the directory `p` resolves to, as if by POSIX `mkdir` with a second argument of `static_cast<int>(perms::all)`. If `mkdir` fails because `p` resolves to an existing directory, no error is reported. Otherwise on failure an error is reported.

**2** *Returns:* `true` if a new directory was created, otherwise `false`.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool create_directory(const path& p, const path& existing_p);
bool create_directory(const path& p, const path& existing_p, error_code& ec) noexcept;
```

**4** *Effects:* Creates the directory `p` resolves to, with attributes copied from directory `existing_p`. The set of attributes copied is operating system dependent. If `mkdir` fails because `p` resolves to an existing directory, no error is reported. Otherwise on failure an error is reported. [*Note 1*: For POSIX-based operating systems, the attributes are those copied by native API `stat(existing_p.c_str(), &attributes_stat)` followed by `mkdir(p.c_str(), attributes_stat.st_mode)`. For Windows-based operating systems, the attributes are those copied by native API `CreateDirectoryExW(existing_p.c_str(), p.c_str(), 0)`. — *end note*]

**5** *Returns:* `true` if a new directory was created with attributes copied from directory `existing_p`, otherwise `false`.

**6** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
