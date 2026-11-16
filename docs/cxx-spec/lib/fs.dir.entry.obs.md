# 29.11.11.3 Observers [fs.dir.entry.obs]

[Contents](../README.md) · up: [29.11.11 Class `directory_entry` \[fs.class.directory.entry\]](fs.class.directory.entry.md) · prev: [29.11.11.2 Modifiers \[fs.dir.entry.mods\]](fs.dir.entry.mods.md) · next: [29.11.12 Class `directory_iterator` \[fs.class.directory.iterator\]](fs.class.directory.iterator.md)

**1** Unqualified function names in the *Returns:* elements of the `directory_entry` observers described below refer to members of the `std::filesystem` namespace.

```cpp
const filesystem::path& path() const noexcept;
operator const filesystem::path&() const noexcept;
```

**2** *Returns:* `pathobject`.

```cpp
bool exists() const;
bool exists(error_code& ec) const noexcept;
```

**3** *Returns:* `exists(this->status())` or `exists(this->status(ec))`, respectively.

**4** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool is_block_file() const;
bool is_block_file(error_code& ec) const noexcept;
```

**5** *Returns:* `is_block_file(this->status())` or `is_block_file(this->status(ec))`, respectively.

**6** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool is_character_file() const;
bool is_character_file(error_code& ec) const noexcept;
```

**7** *Returns:* `is_character_file(this->status())` or `is_character_file(this->status(ec))`, respectively.

**8** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool is_directory() const;
bool is_directory(error_code& ec) const noexcept;
```

**9** *Returns:* `is_directory(this->status())` or `is_directory(this->status(ec))`, respectively.

**10** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool is_fifo() const;
bool is_fifo(error_code& ec) const noexcept;
```

**11** *Returns:* `is_fifo(this->status())` or `is_fifo(this->status(ec))`, respectively.

**12** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool is_other() const;
bool is_other(error_code& ec) const noexcept;
```

**13** *Returns:* `is_other(this->status())` or `is_other(this->status(ec))`, respectively.

**14** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool is_regular_file() const;
bool is_regular_file(error_code& ec) const noexcept;
```

**15** *Returns:* `is_regular_file(this->status())` or `is_regular_file(this->status(ec))`, respectively.

**16** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool is_socket() const;
bool is_socket(error_code& ec) const noexcept;
```

**17** *Returns:* `is_socket(this->status())` or `is_socket(this->status(ec))`, respectively.

**18** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool is_symlink() const;
bool is_symlink(error_code& ec) const noexcept;
```

**19** *Returns:* `is_symlink(this->symlink_status())` or `is_symlink(this->symlink_status(ec))`, respectively.

**20** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
uintmax_t file_size() const;
uintmax_t file_size(error_code& ec) const noexcept;
```

**21** *Returns:* If cached, the file size attribute value. Otherwise, `file_size(path())` or `file_size(path(), ec)`, respectively.

**22** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
uintmax_t hard_link_count() const;
uintmax_t hard_link_count(error_code& ec) const noexcept;
```

**23** *Returns:* If cached, the hard link count attribute value. Otherwise, `hard_link_count(path())` or `hard_link_count(path(), ec)`, respectively.

**24** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
file_time_type last_write_time() const;
file_time_type last_write_time(error_code& ec) const noexcept;
```

**25** *Returns:* If cached, the last write time attribute value. Otherwise, `last_write_time(path())` or `last_write_time(path(), ec)`, respectively.

**26** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
file_status status() const;
file_status status(error_code& ec) const noexcept;
```

**27** *Returns:* If cached, the status attribute value. Otherwise, `status(path())` or `status(path(), ec)`, respectively.

**28** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
file_status symlink_status() const;
file_status symlink_status(error_code& ec) const noexcept;
```

**29** *Returns:* If cached, the symlink status attribute value. Otherwise, `symlink_status(path())` or `symlink_status(path(), ec)`, respectively.

**30** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool operator==(const directory_entry& rhs) const noexcept;
```

**31** *Returns:* `pathobject == rhs.pathobject`.

```cpp
strong_ordering operator<=>(const directory_entry& rhs) const noexcept;
```

**32** *Returns:* `pathobject <=> rhs.pathobject`.
