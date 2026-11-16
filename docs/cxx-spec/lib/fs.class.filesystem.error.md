# 29.11.8 Class `filesystem_error` [fs.class.filesystem.error]

[Contents](../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.7.7 Non-member functions \[fs.path.nonmember\]](fs.path.nonmember.md) · next: [29.11.8.1 Members \[fs.filesystem.error.members\]](fs.filesystem.error.members.md)

```cpp
namespace std::filesystem {
  class filesystem_error : public system_error {
  public:
    filesystem_error(const string& what_arg, error_code ec);
    filesystem_error(const string& what_arg,
                     const path& p1, error_code ec);
    filesystem_error(const string& what_arg,
                     const path& p1, const path& p2, error_code ec);

    const path& path1() const noexcept;
    const path& path2() const noexcept;
    const char* what() const noexcept override;
  };
}
```

**1** The class `filesystem_error` defines the type of objects thrown as exceptions to report file system errors from functions described in subclause [[filesystems]](filesystems.md).

## Subclauses

- [29.11.8.1 Members \[fs.filesystem.error.members\]](fs.filesystem.error.members.md)
