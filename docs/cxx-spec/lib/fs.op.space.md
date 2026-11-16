# 29.11.14.34 Space [fs.op.space]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.33 Resize file \[fs.op.resize.file\]](fs.op.resize.file.md) · next: [29.11.14.35 Status \[fs.op.status\]](fs.op.status.md)

```cpp
space_info space(const path& p);
space_info space(const path& p, error_code& ec) noexcept;
```

**1** *Returns:* An object of type `space_info`. The value of the `space_info` object is determined as if by using POSIX `statvfs` to obtain a POSIX `struct statvfs`, and then multiplying its `f_blocks`, `f_bfree`, and `f_bavail` members by its `f_frsize` member, and assigning the results to the `capacity`, `free`, and `available` members respectively. Any members for which the value cannot be determined shall be set to `static_cast<uintmax_t>(-1)`. For the signature with argument `ec`, all members are set to `static_cast<uintmax_t>(-1)` if an error occurs.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**3** *Remarks:* The value of member `space_info::available` is operating system dependent. [*Note 1*: `available` may be less than `free`. — *end note*]
