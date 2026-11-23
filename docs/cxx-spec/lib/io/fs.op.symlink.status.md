# 29.11.14.37 Symlink status [fs.op.symlink.status]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.36 Status known \[fs.op.status.known\]](fs.op.status.known.md) · next: [29.11.14.38 Temporary directory path \[fs.op.temp.dir.path\]](fs.op.temp.dir.path.md)

```cpp
file_status symlink_status(const path& p);
file_status symlink_status(const path& p, error_code& ec) noexcept;
```

**1** *Effects:* Same as `status()`, above, except that the attributes of `p` are determined as if by using POSIX `lstat()` to obtain a POSIX `struct stat`.

**2** Let `prms` denote the result of `(m & perms::mask)`, where `m` is determined as if by converting the `st_mode` member of the obtained `struct stat` to the type `perms`.

**3** *Returns:* Same as `status()`, above, except that if the attributes indicate a symbolic link, as if by POSIX `S_ISLNK`, returns `file_status(file_type::symlink, prms)`. The signature with argument `ec` returns `file_status(file_type::none)` if an error occurs.

**4** *Remarks:* Pathname resolution terminates if `p` names a symbolic link.

**5** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
