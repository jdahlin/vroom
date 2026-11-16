# 29.11.10 Class `file_status` [fs.class.file.status]

[Contents](../../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.9.6 Enum class `directory_options` \[fs.enum.dir.opts\]](fs.enum.dir.opts.md) · next: [29.11.10.1 Constructors \[fs.file.status.cons\]](fs.file.status.cons.md)

```cpp
namespace std::filesystem {
  class file_status {
  public:
    // [fs.file.status.cons], constructors and destructor
    file_status() noexcept : file_status(file_type::none) {}
    explicit file_status(file_type ft,
                         perms prms = perms::unknown) noexcept;
    file_status(const file_status&) noexcept = default;
    file_status(file_status&&) noexcept = default;
    ~file_status();

    // assignments
    file_status& operator=(const file_status&) noexcept = default;
    file_status& operator=(file_status&&) noexcept = default;

    // [fs.file.status.mods], modifiers
    void       type(file_type ft) noexcept;
    void       permissions(perms prms) noexcept;

    // [fs.file.status.obs], observers
    file_type  type() const noexcept;
    perms      permissions() const noexcept;

    friend bool operator==(const file_status& lhs, const file_status& rhs) noexcept
      { return lhs.type() == rhs.type() && lhs.permissions() == rhs.permissions(); }
  };
}
```

**1** An object of type `file_status` stores information about the type and permissions of a file.

## Subclauses

- [29.11.10.1 Constructors \[fs.file.status.cons\]](fs.file.status.cons.md)
- [29.11.10.2 Observers \[fs.file.status.obs\]](fs.file.status.obs.md)
- [29.11.10.3 Modifiers \[fs.file.status.mods\]](fs.file.status.mods.md)
