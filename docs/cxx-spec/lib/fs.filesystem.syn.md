# 29.11.5 Header `<filesystem>` synopsis [fs.filesystem.syn]

[Contents](../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.4.1 Namespaces and headers \[fs.req.namespace\]](fs.req.namespace.md) · next: [29.11.6 Error reporting \[fs.err.report\]](fs.err.report.md)

```cpp
#include <compare>              // see [compare.syn]

namespace std::filesystem {
  // [fs.class.path], paths
  class path;

  // [fs.path.nonmember], path non-member functions
  void swap(path& lhs, path& rhs) noexcept;
  size_t hash_value(const path& p) noexcept;

  // [fs.class.filesystem.error], filesystem errors
  class filesystem_error;

  // [fs.class.directory.entry], directory entries
  class directory_entry;

  // [fs.class.directory.iterator], directory iterators
  class directory_iterator;

  // [fs.dir.itr.nonmembers], range access for directory iterators
  directory_iterator begin(directory_iterator iter) noexcept;
  directory_iterator end(const directory_iterator&) noexcept;

  // [fs.class.rec.dir.itr], recursive directory iterators
  class recursive_directory_iterator;

  // [fs.rec.dir.itr.nonmembers], range access for recursive directory iterators
  recursive_directory_iterator begin(recursive_directory_iterator iter) noexcept;
  recursive_directory_iterator end(const recursive_directory_iterator&) noexcept;

  // [fs.class.file.status], file status
  class file_status;

  struct space_info {
    uintmax_t capacity;
    uintmax_t free;
    uintmax_t available;

    friend bool operator==(const space_info&, const space_info&) = default;
  };

  // [fs.enum], enumerations
  enum class file_type;
  enum class perms;
  enum class perm_options;
  enum class copy_options;
  enum class directory_options;

  using file_time_type = chrono::time_point<chrono::file_clock>;

  // [fs.op.funcs], filesystem operations
  path absolute(const path& p);
  path absolute(const path& p, error_code& ec);

  path canonical(const path& p);
  path canonical(const path& p, error_code& ec);

  void copy(const path& from, const path& to);
  void copy(const path& from, const path& to, error_code& ec);
  void copy(const path& from, const path& to, copy_options options);
  void copy(const path& from, const path& to, copy_options options,
            error_code& ec);

  bool copy_file(const path& from, const path& to);
  bool copy_file(const path& from, const path& to, error_code& ec);
  bool copy_file(const path& from, const path& to, copy_options option);
  bool copy_file(const path& from, const path& to, copy_options option,
                 error_code& ec);

  void copy_symlink(const path& existing_symlink, const path& new_symlink);
  void copy_symlink(const path& existing_symlink, const path& new_symlink,
                    error_code& ec) noexcept;

  bool create_directories(const path& p);
  bool create_directories(const path& p, error_code& ec);

  bool create_directory(const path& p);
  bool create_directory(const path& p, error_code& ec) noexcept;

  bool create_directory(const path& p, const path& attributes);
  bool create_directory(const path& p, const path& attributes,
                        error_code& ec) noexcept;

  void create_directory_symlink(const path& to, const path& new_symlink);
  void create_directory_symlink(const path& to, const path& new_symlink,
                                error_code& ec) noexcept;

  void create_hard_link(const path& to, const path& new_hard_link);
  void create_hard_link(const path& to, const path& new_hard_link,
                        error_code& ec) noexcept;

  void create_symlink(const path& to, const path& new_symlink);
  void create_symlink(const path& to, const path& new_symlink,
                      error_code& ec) noexcept;

  path current_path();
  path current_path(error_code& ec);
  void current_path(const path& p);
  void current_path(const path& p, error_code& ec) noexcept;

  bool equivalent(const path& p1, const path& p2);
  bool equivalent(const path& p1, const path& p2, error_code& ec) noexcept;

  bool exists(file_status s) noexcept;
  bool exists(const path& p);
  bool exists(const path& p, error_code& ec) noexcept;

  uintmax_t file_size(const path& p);
  uintmax_t file_size(const path& p, error_code& ec) noexcept;

  uintmax_t hard_link_count(const path& p);
  uintmax_t hard_link_count(const path& p, error_code& ec) noexcept;

  bool is_block_file(file_status s) noexcept;
  bool is_block_file(const path& p);
  bool is_block_file(const path& p, error_code& ec) noexcept;

  bool is_character_file(file_status s) noexcept;
  bool is_character_file(const path& p);
  bool is_character_file(const path& p, error_code& ec) noexcept;

  bool is_directory(file_status s) noexcept;
  bool is_directory(const path& p);
  bool is_directory(const path& p, error_code& ec) noexcept;

  bool is_empty(const path& p);
  bool is_empty(const path& p, error_code& ec);

  bool is_fifo(file_status s) noexcept;
  bool is_fifo(const path& p);
  bool is_fifo(const path& p, error_code& ec) noexcept;

  bool is_other(file_status s) noexcept;
  bool is_other(const path& p);
  bool is_other(const path& p, error_code& ec) noexcept;

  bool is_regular_file(file_status s) noexcept;
  bool is_regular_file(const path& p);
  bool is_regular_file(const path& p, error_code& ec) noexcept;

  bool is_socket(file_status s) noexcept;
  bool is_socket(const path& p);
  bool is_socket(const path& p, error_code& ec) noexcept;

  bool is_symlink(file_status s) noexcept;
  bool is_symlink(const path& p);
  bool is_symlink(const path& p, error_code& ec) noexcept;

  file_time_type last_write_time(const path& p);
  file_time_type last_write_time(const path& p, error_code& ec) noexcept;
  void last_write_time(const path& p, file_time_type new_time);
  void last_write_time(const path& p, file_time_type new_time,
                       error_code& ec) noexcept;

  void permissions(const path& p, perms prms, perm_options opts=perm_options::replace);
  void permissions(const path& p, perms prms, error_code& ec) noexcept;
  void permissions(const path& p, perms prms, perm_options opts, error_code& ec);

  path proximate(const path& p, error_code& ec);
  path proximate(const path& p, const path& base = current_path());
  path proximate(const path& p, const path& base, error_code& ec);

  path read_symlink(const path& p);
  path read_symlink(const path& p, error_code& ec);

  path relative(const path& p, error_code& ec);
  path relative(const path& p, const path& base = current_path());
  path relative(const path& p, const path& base, error_code& ec);

  bool remove(const path& p);
  bool remove(const path& p, error_code& ec) noexcept;

  uintmax_t remove_all(const path& p);
  uintmax_t remove_all(const path& p, error_code& ec);

  void rename(const path& from, const path& to);
  void rename(const path& from, const path& to, error_code& ec) noexcept;

  void resize_file(const path& p, uintmax_t size);
  void resize_file(const path& p, uintmax_t size, error_code& ec) noexcept;

  space_info space(const path& p);
  space_info space(const path& p, error_code& ec) noexcept;

  file_status status(const path& p);
  file_status status(const path& p, error_code& ec) noexcept;

  bool status_known(file_status s) noexcept;

  file_status symlink_status(const path& p);
  file_status symlink_status(const path& p, error_code& ec) noexcept;

  path temp_directory_path();
  path temp_directory_path(error_code& ec);

  path weakly_canonical(const path& p);
  path weakly_canonical(const path& p, error_code& ec);
}
```

**1** Implementations should ensure that the resolution and range of `file_time_type` reflect the operating system dependent resolution and range of file time values.
