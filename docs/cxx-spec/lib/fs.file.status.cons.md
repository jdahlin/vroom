# 29.11.10.1 Constructors [fs.file.status.cons]

[Contents](../README.md) · up: [29.11.10 Class `file_status` \[fs.class.file.status\]](fs.class.file.status.md) · prev: [29.11.10 Class `file_status` \[fs.class.file.status\]](fs.class.file.status.md) · next: [29.11.10.2 Observers \[fs.file.status.obs\]](fs.file.status.obs.md)

```cpp
explicit file_status(file_type ft, perms prms = perms::unknown) noexcept;
```

**1** *Postconditions:* `type() == ft` and `permissions() == prms`.
