# 29.11.13.1 Members [fs.rec.dir.itr.members]

[Contents](../README.md) · up: [29.11.13 Class `recursive_directory_iterator` \[fs.class.rec.dir.itr\]](fs.class.rec.dir.itr.md) · prev: [29.11.13 Class `recursive_directory_iterator` \[fs.class.rec.dir.itr\]](fs.class.rec.dir.itr.md) · next: [29.11.13.2 Non-member functions \[fs.rec.dir.itr.nonmembers\]](fs.rec.dir.itr.nonmembers.md)

```cpp
recursive_directory_iterator() noexcept;
```

**1** *Effects:* Constructs the end iterator.

```cpp
explicit recursive_directory_iterator(const path& p);
recursive_directory_iterator(const path& p, directory_options options);
recursive_directory_iterator(const path& p, directory_options options, error_code& ec);
recursive_directory_iterator(const path& p, error_code& ec);
```

**2** *Effects:* Constructs an iterator representing the first entry in the directory to which `p` resolves, if any; otherwise, the end iterator. However, if

```cpp
(options & directory_options::skip_permission_denied) != directory_options::none
```

and construction encounters an error indicating that permission to access `p` is denied, constructs the end iterator and does not report an error.

**3** *Postconditions:* `options() == options` for the signatures with a `directory_options` argument, otherwise `options() == directory_options::none`.

**4** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**5** [*Note 1*: To iterate over the current directory, use `recursive_directory_iterator(".")` rather than `recursive_directory_iterator("")`. — *end note*]

**6** [*Note 2*: By default, `recursive_directory_iterator` does not follow directory symlinks. To follow directory symlinks, specify `options` as `directory_options::follow_directory_symlink`. — *end note*]

```cpp
recursive_directory_iterator(const recursive_directory_iterator& rhs);
```

**7** *Postconditions:*

- `options() == rhs.options()`
- `depth() == rhs.depth()`
- `recursion_pending() == rhs.recursion_pending()`

```cpp
recursive_directory_iterator(recursive_directory_iterator&& rhs) noexcept;
```

**8** *Postconditions:* `options()`, `depth()`, and `recursion_pending()` have the values that `rhs.options()`, `rhs.depth()`, and `rhs.recursion_pending()`, respectively, had before the function call.

```cpp
recursive_directory_iterator& operator=(const recursive_directory_iterator& rhs);
```

**9** *Effects:* If `*this` and `rhs` are the same object, the member has no effect.

**10** *Postconditions:*

- `options() == rhs.options()`
- `depth() == rhs.depth()`
- `recursion_pending() == rhs.recursion_pending()`

**11** *Returns:* `*this`.

```cpp
recursive_directory_iterator& operator=(recursive_directory_iterator&& rhs) noexcept;
```

**12** *Effects:* If `*this` and `rhs` are the same object, the member has no effect.

**13** *Postconditions:* `options()`, `depth()`, and `recursion_pending()` have the values that `rhs.options()`, `rhs.depth()`, and `rhs.recursion_pending()`, respectively, had before the function call.

**14** *Returns:* `*this`.

```cpp
directory_options options() const;
```

**15** *Returns:* The value of the argument passed to the constructor for the `options` parameter, if present, otherwise `directory_options::none`.

**16** *Throws:* Nothing.

```cpp
int depth() const;
```

**17** *Returns:* The current depth of the directory tree being traversed. [*Note 3*: The initial directory is depth `0`, its immediate subdirectories are depth `1`, and so forth. — *end note*]

**18** *Throws:* Nothing.

```cpp
bool recursion_pending() const;
```

**19** *Returns:* `true` if `disable_recursion_pending()` has not been called subsequent to the prior construction or increment operation, otherwise `false`.

**20** *Throws:* Nothing.

```cpp
recursive_directory_iterator& operator++();
recursive_directory_iterator& increment(error_code& ec);
```

**21** *Effects:* As specified for the prefix increment operation of Input iterators ([[input.iterators]](input.iterators.md)), except that:

- If there are no more entries at the current depth, then if `depth() != 0` iteration over the parent directory resumes; otherwise `*this = recursive_directory_iterator()`.

- Otherwise if

  ```cpp
  recursion_pending() && is_directory((*this)->status()) &&
  (!is_symlink((*this)->symlink_status()) ||
   (options() & directory_options::follow_directory_symlink) != directory_options::none)
  ```

  then either directory `(*this)->path()` is recursively iterated into or, if

  ```cpp
  (options() & directory_options::skip_permission_denied) != directory_options::none
  ```

  and an error occurs indicating that permission to access directory `(*this)->path()` is denied, then directory `(*this)->path()` is treated as an empty directory and no error is reported.

**22** *Returns:* `*this`.

**23** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
void pop();
void pop(error_code& ec);
```

**24** *Effects:* If `depth() == 0`, set `*this` to `recursive_directory_iterator()`. Otherwise, cease iteration of the directory currently being iterated over, and continue iteration over the parent directory.

**25** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**26** *Remarks:* Any copies of the previous value of `*this` are no longer required to be dereferenceable nor to be in the domain of `==`.

```cpp
void disable_recursion_pending();
```

**27** *Postconditions:* `recursion_pending() == false`.

**28** [*Note 4*: `disable_recursion_pending``()` is used to prevent unwanted recursion into a directory. — *end note*]
