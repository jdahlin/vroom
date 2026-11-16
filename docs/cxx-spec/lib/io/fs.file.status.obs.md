# 29.11.10.2 Observers [fs.file.status.obs]

[Contents](../../README.md) · up: [29.11.10 Class `file_status` \[fs.class.file.status\]](fs.class.file.status.md) · prev: [29.11.10.1 Constructors \[fs.file.status.cons\]](fs.file.status.cons.md) · next: [29.11.10.3 Modifiers \[fs.file.status.mods\]](fs.file.status.mods.md)

```cpp
file_type type() const noexcept;
```

**1** *Returns:* The value of `type()` specified by the postconditions of the most recent call to a constructor, `operator=`, or `type(file_type)` function.

```cpp
perms permissions() const noexcept;
```

**2** *Returns:* The value of `permissions()` specified by the postconditions of the most recent call to a constructor, `operator=`, or `permissions(perms)` function.
