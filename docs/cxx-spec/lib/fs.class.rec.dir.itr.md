# 29.11.13 Class `recursive_directory_iterator` [fs.class.rec.dir.itr]

[Contents](../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.12.2 Non-member functions \[fs.dir.itr.nonmembers\]](fs.dir.itr.nonmembers.md) · next: [29.11.13.1 Members \[fs.rec.dir.itr.members\]](fs.rec.dir.itr.members.md)

**1** An object of type `recursive_directory_iterator` provides an iterator for a sequence of `directory_entry` elements representing the files in a directory or in an implementation-defined directory-like file type, and its sub-directories.

```cpp
namespace std::filesystem {
  class recursive_directory_iterator {
  public:
    using iterator_category = input_iterator_tag;
    using value_type        = directory_entry;
    using difference_type   = ptrdiff_t;
    using pointer           = const directory_entry*;
    using reference         = const directory_entry&;

    // [fs.rec.dir.itr.members], constructors and destructor
    recursive_directory_iterator() noexcept;
    explicit recursive_directory_iterator(const path& p);
    recursive_directory_iterator(const path& p, directory_options options);
    recursive_directory_iterator(const path& p, directory_options options,
                                 error_code& ec);
    recursive_directory_iterator(const path& p, error_code& ec);
    recursive_directory_iterator(const recursive_directory_iterator& rhs);
    recursive_directory_iterator(recursive_directory_iterator&& rhs) noexcept;
    ~recursive_directory_iterator();

    // [fs.rec.dir.itr.members], observers
    directory_options  options() const;
    int                depth() const;
    bool               recursion_pending() const;

    const directory_entry& operator*() const;
    const directory_entry* operator->() const;

    // [fs.rec.dir.itr.members], modifiers
    recursive_directory_iterator&
      operator=(const recursive_directory_iterator& rhs);
    recursive_directory_iterator&
      operator=(recursive_directory_iterator&& rhs) noexcept;

    recursive_directory_iterator& operator++();
    recursive_directory_iterator& increment(error_code& ec);

    void pop();
    void pop(error_code& ec);
    void disable_recursion_pending();

    // other members as required by [input.iterators], input iterators
  };
}
```

**2** Calling `options`, `depth`, `recursion_pending`, `pop` or `disable_recursion_pending` on an iterator that is not dereferenceable results in undefined behavior.

**3** The behavior of a `recursive_directory_iterator` is the same as a `directory_iterator` unless otherwise specified.

**4** [*Note 1*: If the directory structure being iterated over contains cycles then the end iterator may be unreachable. — *end note*]

## Subclauses

- [29.11.13.1 Members \[fs.rec.dir.itr.members\]](fs.rec.dir.itr.members.md)
- [29.11.13.2 Non-member functions \[fs.rec.dir.itr.nonmembers\]](fs.rec.dir.itr.nonmembers.md)
