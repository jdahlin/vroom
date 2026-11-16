# 29.11.11.2 Modifiers [fs.dir.entry.mods]

[Contents](../README.md) · up: [29.11.11 Class `directory_entry` \[fs.class.directory.entry\]](fs.class.directory.entry.md) · prev: [29.11.11.1 Constructors \[fs.dir.entry.cons\]](fs.dir.entry.cons.md) · next: [29.11.11.3 Observers \[fs.dir.entry.obs\]](fs.dir.entry.obs.md)

```cpp
void assign(const filesystem::path& p);
void assign(const filesystem::path& p, error_code& ec);
```

**1** *Effects:* Equivalent to `pathobject = p`, then `refresh()` or `refresh(ec)`, respectively. If an error occurs, the values of any cached attributes are unspecified.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
void replace_filename(const filesystem::path& p);
void replace_filename(const filesystem::path& p, error_code& ec);
```

**3** *Effects:* Equivalent to `pathobject.replace_filename(p)`, then `refresh()` or `refresh(ec)`, respectively. If an error occurs, the values of any cached attributes are unspecified.

*Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
void refresh();
void refresh(error_code& ec) noexcept;
```

**4** *Effects:* Stores the current values of any cached attributes of the file `p` resolves to. If an error occurs, an error is reported ([[fs.err.report]](fs.err.report.md)) and the values of any cached attributes are unspecified.

**5** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**6** [*Note 1*: Implementations of `directory_iterator` ([[fs.class.directory.iterator]](fs.class.directory.iterator.md)) are prohibited from directly or indirectly calling the `refresh` function since it must access the external file system, and the objective of caching is to avoid unnecessary file system accesses. — *end note*]
