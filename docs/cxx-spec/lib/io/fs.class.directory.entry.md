# 29.11.11 Class `directory_entry` [fs.class.directory.entry]

[Contents](../../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.10.3 Modifiers \[fs.file.status.mods\]](fs.file.status.mods.md) · next: [29.11.11.1 Constructors \[fs.dir.entry.cons\]](fs.dir.entry.cons.md)

```cpp
namespace std::filesystem {
  class directory_entry {
  public:
    // [fs.dir.entry.cons], constructors and destructor
    directory_entry() noexcept = default;
    directory_entry(const directory_entry&) = default;
    directory_entry(directory_entry&&) noexcept = default;
    explicit directory_entry(const filesystem::path& p);
    directory_entry(const filesystem::path& p, error_code& ec);
    ~directory_entry();

    // assignments
    directory_entry& operator=(const directory_entry&) = default;
    directory_entry& operator=(directory_entry&&) noexcept = default;

    // [fs.dir.entry.mods], modifiers
    void assign(const filesystem::path& p);
    void assign(const filesystem::path& p, error_code& ec);
    void replace_filename(const filesystem::path& p);
    void replace_filename(const filesystem::path& p, error_code& ec);
    void refresh();
    void refresh(error_code& ec) noexcept;

    // [fs.dir.entry.obs], observers
    const filesystem::path& path() const noexcept;
    operator const filesystem::path&() const noexcept;
    bool exists() const;
    bool exists(error_code& ec) const noexcept;
    bool is_block_file() const;
    bool is_block_file(error_code& ec) const noexcept;
    bool is_character_file() const;
    bool is_character_file(error_code& ec) const noexcept;
    bool is_directory() const;
    bool is_directory(error_code& ec) const noexcept;
    bool is_fifo() const;
    bool is_fifo(error_code& ec) const noexcept;
    bool is_other() const;
    bool is_other(error_code& ec) const noexcept;
    bool is_regular_file() const;
    bool is_regular_file(error_code& ec) const noexcept;
    bool is_socket() const;
    bool is_socket(error_code& ec) const noexcept;
    bool is_symlink() const;
    bool is_symlink(error_code& ec) const noexcept;
    uintmax_t file_size() const;
    uintmax_t file_size(error_code& ec) const noexcept;
    uintmax_t hard_link_count() const;
    uintmax_t hard_link_count(error_code& ec) const noexcept;
    file_time_type last_write_time() const;
    file_time_type last_write_time(error_code& ec) const noexcept;
    file_status status() const;
    file_status status(error_code& ec) const noexcept;
    file_status symlink_status() const;
    file_status symlink_status(error_code& ec) const noexcept;

    bool operator==(const directory_entry& rhs) const noexcept;
    strong_ordering operator<=>(const directory_entry& rhs) const noexcept;

  private:
    filesystem::path pathobject;        // exposition only
    friend class directory_iterator;    // exposition only
  };
}
```

**1** A `directory_entry` object stores a `path` object and may store additional objects for file attributes such as hard link count, status, symlink status, file size, and last write time.

**2** Implementations should store such additional file attributes during directory iteration if their values are available and storing the values would allow the implementation to eliminate file system accesses by `directory_entry` observer functions ([[fs.op.funcs]](fs.op.funcs.md)). Such stored file attribute values are said to be *cached*.

**3** [*Note 1*: For purposes of exposition, class `directory_iterator` ([[fs.class.directory.iterator]](fs.class.directory.iterator.md)) is shown above as a friend of class `directory_entry`. Friendship allows the `directory_iterator` implementation to cache already available attribute values directly into a `directory_entry` object without the cost of an unneeded call to `refresh()`. — *end note*]

**4** [*Example 1*:

```cpp
using namespace std::filesystem;

// use possibly cached last write time to minimize disk accesses
for (auto&& x : directory_iterator("."))
{
  std::cout << x.path() << " " << x.last_write_time() << std::endl;
}

// call refresh() to refresh a stale cache
for (auto&& x : directory_iterator("."))
{
  lengthy_function(x.path());   // cache becomes stale
  x.refresh();
  std::cout << x.path() << " " << x.last_write_time() << std::endl;
}
```

On implementations that do not cache the last write time, both loops will result in a potentially expensive call to the `std::filesystem::last_write_time` function. On implementations that do cache the last write time, the first loop will use the cached value and so will not result in a potentially expensive call to the `std::filesystem::last_write_time` function. The code is portable to any implementation, regardless of whether or not it employs caching. — *end example*]

## Subclauses

- [29.11.11.1 Constructors \[fs.dir.entry.cons\]](fs.dir.entry.cons.md)
- [29.11.11.2 Modifiers \[fs.dir.entry.mods\]](fs.dir.entry.mods.md)
- [29.11.11.3 Observers \[fs.dir.entry.obs\]](fs.dir.entry.obs.md)
