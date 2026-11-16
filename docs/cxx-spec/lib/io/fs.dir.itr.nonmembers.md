# 29.11.12.2 Non-member functions [fs.dir.itr.nonmembers]

[Contents](../../README.md) · up: [29.11.12 Class `directory_iterator` \[fs.class.directory.iterator\]](fs.class.directory.iterator.md) · prev: [29.11.12.1 Members \[fs.dir.itr.members\]](fs.dir.itr.members.md) · next: [29.11.13 Class `recursive_directory_iterator` \[fs.class.rec.dir.itr\]](fs.class.rec.dir.itr.md)

**1** These functions enable range access for `directory_iterator`.

```cpp
directory_iterator begin(directory_iterator iter) noexcept;
```

**2** *Returns:* `iter`.

```cpp
directory_iterator end(const directory_iterator&) noexcept;
```

**3** *Returns:* `directory_iterator()`.
