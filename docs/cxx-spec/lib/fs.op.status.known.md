# 29.11.14.36 Status known [fs.op.status.known]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.35 Status \[fs.op.status\]](fs.op.status.md) · next: [29.11.14.37 Symlink status \[fs.op.symlink.status\]](fs.op.symlink.status.md)

```cpp
bool status_known(file_status s) noexcept;
```

**1** *Returns:* `s.type() != file_type::none`.
