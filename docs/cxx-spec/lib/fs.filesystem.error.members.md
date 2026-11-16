# 29.11.8.1 Members [fs.filesystem.error.members]

[Contents](../README.md) · up: [29.11.8 Class `filesystem_error` \[fs.class.filesystem.error\]](fs.class.filesystem.error.md) · prev: [29.11.8 Class `filesystem_error` \[fs.class.filesystem.error\]](fs.class.filesystem.error.md) · next: [29.11.9 Enumerations \[fs.enum\]](fs.enum.md)

**1** Constructors are provided that store zero, one, or two paths associated with an error.

```cpp
filesystem_error(const string& what_arg, error_code ec);
```

**2** *Postconditions:*

- `code() == ec`,
- `path1().empty() == true`,
- `path2().empty() == true`, and
- `string_view(what()).find(what_arg.c_str())` `!= string_view::npos`.

```cpp
filesystem_error(const string& what_arg, const path& p1, error_code ec);
```

**3** *Postconditions:*

- `code() == ec`,
- `path1()` returns a reference to the stored copy of `p1`,
- `path2().empty() == true`, and
- `string_view(what()).find(what_arg.c_str())` `!= string_view::npos`.

```cpp
filesystem_error(const string& what_arg, const path& p1, const path& p2, error_code ec);
```

**4** *Postconditions:*

- `code() == ec`,
- `path1()` returns a reference to the stored copy of `p1`,
- `path2()` returns a reference to the stored copy of `p2`, and
- `string_view(what()).find(what_arg.c_str())` `!= string_view::npos`.

```cpp
const path& path1() const noexcept;
```

**5** *Returns:* A reference to the copy of `p1` stored by the constructor, or, if none, an empty path.

```cpp
const path& path2() const noexcept;
```

**6** *Returns:* A reference to the copy of `p2` stored by the constructor, or, if none, an empty path.

```cpp
const char* what() const noexcept override;
```

**7** *Returns:* An NTBS that incorporates the `what_arg` argument supplied to the constructor. The exact format is unspecified. Implementations should include the `system_error::what()` string and the pathnames of `path1` and `path2` in the native format in the returned string.
