# 29.11.14.25 Last write time [fs.op.last.write.time]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.24 Is symlink \[fs.op.is.symlink\]](fs.op.is.symlink.md) · next: [29.11.14.26 Permissions \[fs.op.permissions\]](fs.op.permissions.md)

```cpp
file_time_type last_write_time(const path& p);
file_time_type last_write_time(const path& p, error_code& ec) noexcept;
```

**1** *Returns:* The time of last data modification of `p`, determined as if by the value of the POSIX `stat` class member `st_mtime` obtained as if by POSIX `stat()`. The signature with argument `ec` returns `file_time_type::min()` if an error occurs.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
void last_write_time(const path& p, file_time_type new_time);
void last_write_time(const path& p, file_time_type new_time,
                     error_code& ec) noexcept;
```

**3** *Effects:* Sets the time of last data modification of the file resolved to by `p` to `new_time`, as if by POSIX `futimens()`.

**4** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**5** [*Note 1*: A postcondition of `last_write_time(p) == new_time` is not specified since it might not hold for file systems with coarse time granularity. — *end note*]
