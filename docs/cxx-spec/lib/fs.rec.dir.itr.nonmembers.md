# 29.11.13.2 Non-member functions [fs.rec.dir.itr.nonmembers]

[Contents](../README.md) · up: [29.11.13 Class `recursive_directory_iterator` \[fs.class.rec.dir.itr\]](fs.class.rec.dir.itr.md) · prev: [29.11.13.1 Members \[fs.rec.dir.itr.members\]](fs.rec.dir.itr.members.md) · next: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md)

**1** These functions enable use of `recursive_directory_iterator` with range-based for statements.

```cpp
recursive_directory_iterator begin(recursive_directory_iterator iter) noexcept;
```

**2** *Returns:* `iter`.

```cpp
recursive_directory_iterator end(const recursive_directory_iterator&) noexcept;
```

**3** *Returns:* `recursive_directory_iterator()`.
