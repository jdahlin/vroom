# 29.11.7.4.4 Concatenation [fs.path.concat]

[Contents](../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.3 Appends \[fs.path.append\]](fs.path.append.md) · next: [29.11.7.4.5 Modifiers \[fs.path.modifiers\]](fs.path.modifiers.md)

```cpp
path& operator+=(const path& x);
path& operator+=(const string_type& x);
path& operator+=(basic_string_view<value_type> x);
path& operator+=(const value_type* x);
template<class Source>
  path& operator+=(const Source& x);
template<class Source>
  path& concat(const Source& x);
```

**1** *Effects:* Appends `path(x).native()` to the pathname in the native format. [*Note 1*: This directly manipulates the value of `native()` and may not be portable between operating systems. — *end note*]

**2** *Returns:* `*this`.

```cpp
path& operator+=(value_type x);
template<class EcharT>
  path& operator+=(EcharT x);
```

**3** *Effects:* Equivalent to: `return *this += basic_string_view(&x, 1);`

```cpp
template<class InputIterator>
  path& concat(InputIterator first, InputIterator last);
```

**4** *Effects:* Equivalent to: `return *this += path(first, last);`
