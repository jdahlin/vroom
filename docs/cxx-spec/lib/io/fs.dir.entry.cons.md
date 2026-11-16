# 29.11.11.1 Constructors [fs.dir.entry.cons]

[Contents](../../README.md) · up: [29.11.11 Class `directory_entry` \[fs.class.directory.entry\]](fs.class.directory.entry.md) · prev: [29.11.11 Class `directory_entry` \[fs.class.directory.entry\]](fs.class.directory.entry.md) · next: [29.11.11.2 Modifiers \[fs.dir.entry.mods\]](fs.dir.entry.mods.md)

```cpp
explicit directory_entry(const filesystem::path& p);
directory_entry(const filesystem::path& p, error_code& ec);
```

**1** *Effects:* Calls `refresh()` or `refresh(ec)`, respectively.

**2** *Postconditions:* `path() == p` if no error occurs, otherwise `path() == filesystem::path()`.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
