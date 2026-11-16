# 29.11.7.4.2 Assignments [fs.path.assign]

[Contents](../../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.1 Constructors \[fs.path.construct\]](fs.path.construct.md) · next: [29.11.7.4.3 Appends \[fs.path.append\]](fs.path.append.md)

```cpp
path& operator=(const path& p);
```

**1** *Effects:* If `*this` and `p` are the same object, has no effect. Otherwise, sets both respective pathnames of `*this` to the respective pathnames of `p`.

**2** *Returns:* `*this`.

```cpp
path& operator=(path&& p) noexcept;
```

**3** *Effects:* If `*this` and `p` are the same object, has no effect. Otherwise, sets both respective pathnames of `*this` to the respective pathnames of `p`. `p` is left in a valid but unspecified state. [*Note 1*: A valid implementation is `swap(p)`. — *end note*]

**4** *Returns:* `*this`.

```cpp
path& operator=(string_type&& source);
path& assign(string_type&& source);
```

**5** *Effects:* Sets the pathname in the detected-format of `source` to the original value of `source`. `source` is left in a valid but unspecified state.

**6** *Returns:* `*this`.

```cpp
template<class Source>
  path& operator=(const Source& source);
template<class Source>
  path& assign(const Source& source);
template<class InputIterator>
  path& assign(InputIterator first, InputIterator last);
```

**7** *Effects:* Let `s` be the effective range of `source` ([[fs.path.req]](fs.path.req.md)) or the range `[first, last)`, with the encoding converted if required ([[fs.path.cvt]](fs.path.cvt.md)). Finds the detected-format of `s` ([[fs.path.fmt.cvt]](fs.path.fmt.cvt.md)) and sets the pathname in that format to `s`.

**8** *Returns:* `*this`.
