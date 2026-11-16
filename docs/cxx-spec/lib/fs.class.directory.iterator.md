# 29.11.12 Class `directory_iterator` [fs.class.directory.iterator]

[Contents](../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.11.3 Observers \[fs.dir.entry.obs\]](fs.dir.entry.obs.md) · next: [29.11.12.1 Members \[fs.dir.itr.members\]](fs.dir.itr.members.md)

**1** An object of type `directory_iterator` provides an iterator for a sequence of `directory_entry` elements representing the path and any cached attribute values ([[fs.class.directory.entry]](fs.class.directory.entry.md)) for each file in a directory or in an implementation-defined directory-like file type. [*Note 1*: For iteration into sub-directories, see class `recursive_directory_iterator` ([[fs.class.rec.dir.itr]](fs.class.rec.dir.itr.md)). — *end note*]

```cpp
namespace std::filesystem {
  class directory_iterator {
  public:
    using iterator_category = input_iterator_tag;
    using value_type        = directory_entry;
    using difference_type   = ptrdiff_t;
    using pointer           = const directory_entry*;
    using reference         = const directory_entry&;

    // [fs.dir.itr.members], member functions
    directory_iterator() noexcept;
    explicit directory_iterator(const path& p);
    directory_iterator(const path& p, directory_options options);
    directory_iterator(const path& p, error_code& ec);
    directory_iterator(const path& p, directory_options options,
                       error_code& ec);
    directory_iterator(const directory_iterator& rhs);
    directory_iterator(directory_iterator&& rhs) noexcept;
    ~directory_iterator();

    directory_iterator& operator=(const directory_iterator& rhs);
    directory_iterator& operator=(directory_iterator&& rhs) noexcept;

    const directory_entry& operator*() const;
    const directory_entry* operator->() const;
    directory_iterator&    operator++();
    directory_iterator&    increment(error_code& ec);

    // other members as required by [input.iterators], input iterators
  };
}
```

**2** `directory_iterator` meets the *Cpp17InputIterator* requirements ([[input.iterators]](input.iterators.md)).

**3** If an iterator of type `directory_iterator` reports an error or is advanced past the last directory element, that iterator shall become equal to the end iterator value. The `directory_iterator` default constructor shall create an iterator equal to the end iterator value, and this shall be the only valid iterator for the end condition.

**4** The end iterator is not dereferenceable.

**5** Two end iterators are always equal. An end iterator shall not be equal to a non-end iterator.

**6** The result of calling the `path()` member of the `directory_entry` object obtained by dereferencing a `directory_iterator` is a reference to a `path` object composed of the directory argument from which the iterator was constructed with filename of the directory entry appended as if by `operator/=`.

**7** Directory iteration shall not yield directory entries for the current (dot) and parent (dot-dot) directories.

**8** The order of directory entries obtained by dereferencing successive increments of a `directory_iterator` is unspecified.

**9** Constructors and non-const `directory_iterator` member functions store the values of any cached attributes ([[fs.class.directory.entry]](fs.class.directory.entry.md)) in the `directory_entry` element returned by `operator*()`. `directory_iterator` member functions shall not directly or indirectly call any `directory_entry` `refresh` function. [*Note 2*: The exact mechanism for storing cached attribute values is not exposed to users. For exposition, class `directory_iterator` is shown in [[fs.class.directory.entry]](fs.class.directory.entry.md) as a friend of class `directory_entry`. — *end note*]

**10** [*Note 3*: Programs performing directory iteration may wish to test if the path obtained by dereferencing a directory iterator actually exists. It could be a symbolic link to a non-existent file. Programs recursively walking directory trees for purposes of removing and renaming entries may wish to avoid following symbolic links. — *end note*]

**11** [*Note 4*: If a file is removed from or added to a directory after the construction of a `directory_iterator` for the directory, it is unspecified whether or not subsequently incrementing the iterator will ever result in an iterator referencing the removed or added directory entry. See POSIX `readdir_r`. — *end note*]

## Subclauses

- [29.11.12.1 Members \[fs.dir.itr.members\]](fs.dir.itr.members.md)
- [29.11.12.2 Non-member functions \[fs.dir.itr.nonmembers\]](fs.dir.itr.nonmembers.md)
