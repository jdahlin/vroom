# 29.11.10.3 Modifiers [fs.file.status.mods]

[Contents](../README.md) · up: [29.11.10 Class `file_status` \[fs.class.file.status\]](fs.class.file.status.md) · prev: [29.11.10.2 Observers \[fs.file.status.obs\]](fs.file.status.obs.md) · next: [29.11.11 Class `directory_entry` \[fs.class.directory.entry\]](fs.class.directory.entry.md)

```cpp
void type(file_type ft) noexcept;
```

**1** *Postconditions:* `type() == ft`.

```cpp
void permissions(perms prms) noexcept;
```

**2** *Postconditions:* `permissions() == prms`.
