# 29.11.7.4.10 Query [fs.path.query]

[Contents](../../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.9 Decomposition \[fs.path.decompose\]](fs.path.decompose.md) · next: [29.11.7.4.11 Generation \[fs.path.gen\]](fs.path.gen.md)

```cpp
[[nodiscard]] bool empty() const noexcept;
```

**1** *Returns:* `true` if the pathname in the generic format is empty, otherwise `false`.

```cpp
bool has_root_path() const;
```

**2** *Returns:* `!root_path().empty()`.

```cpp
bool has_root_name() const;
```

**3** *Returns:* `!root_name().empty()`.

```cpp
bool has_root_directory() const;
```

**4** *Returns:* `!root_directory().empty()`.

```cpp
bool has_relative_path() const;
```

**5** *Returns:* `!relative_path().empty()`.

```cpp
bool has_parent_path() const;
```

**6** *Returns:* `!parent_path().empty()`.

```cpp
bool has_filename() const;
```

**7** *Returns:* `!filename().empty()`.

```cpp
bool has_stem() const;
```

**8** *Returns:* `!stem().empty()`.

```cpp
bool has_extension() const;
```

**9** *Returns:* `!extension().empty()`.

```cpp
bool is_absolute() const;
```

**10** *Returns:* `true` if the pathname in the native format contains an absolute path ([[fs.class.path]](fs.class.path.md)), otherwise `false`.

**11** [*Example 1*: `path("/").is_absolute()` is `true` for POSIX-based operating systems, and `false` for Windows-based operating systems. — *end example*]

```cpp
bool is_relative() const;
```

**12** *Returns:* `!is_absolute()`.
