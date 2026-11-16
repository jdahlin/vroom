# 29.11.12.1 Members [fs.dir.itr.members]

[Contents](../README.md) · up: [29.11.12 Class `directory_iterator` \[fs.class.directory.iterator\]](fs.class.directory.iterator.md) · prev: [29.11.12 Class `directory_iterator` \[fs.class.directory.iterator\]](fs.class.directory.iterator.md) · next: [29.11.12.2 Non-member functions \[fs.dir.itr.nonmembers\]](fs.dir.itr.nonmembers.md)

```cpp
directory_iterator() noexcept;
```

**1** *Effects:* Constructs the end iterator.

```cpp
explicit directory_iterator(const path& p);
directory_iterator(const path& p, directory_options options);
directory_iterator(const path& p, error_code& ec);
directory_iterator(const path& p, directory_options options, error_code& ec);
```

**2** *Effects:* For the directory that `p` resolves to, constructs an iterator for the first element in a sequence of `directory_entry` elements representing the files in the directory, if any; otherwise the end iterator. However, if

```cpp
(options & directory_options::skip_permission_denied) != directory_options::none
```

and construction encounters an error indicating that permission to access `p` is denied, constructs the end iterator and does not report an error.

**3** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**4** [*Note 1*: To iterate over the current directory, use `directory_iterator(".")` rather than `directory_iterator("")`. — *end note*]

```cpp
directory_iterator(const directory_iterator& rhs);
directory_iterator(directory_iterator&& rhs) noexcept;
```

**5** *Postconditions:* `*this` has the original value of `rhs`.

```cpp
directory_iterator& operator=(const directory_iterator& rhs);
directory_iterator& operator=(directory_iterator&& rhs) noexcept;
```

**6** *Effects:* If `*this` and `rhs` are the same object, the member has no effect.

**7** *Postconditions:* `*this` has the original value of `rhs`.

**8** *Returns:* `*this`.

```cpp
directory_iterator& operator++();
directory_iterator& increment(error_code& ec);
```

**9** *Effects:* As specified for the prefix increment operation of Input iterators ([[input.iterators]](input.iterators.md)).

**10** *Returns:* `*this`.

**11** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
