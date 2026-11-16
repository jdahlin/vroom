# 29.11.7.6 Inserter and extractor [fs.path.io]

[Contents](../README.md) · up: [29.11.7 Class `path` \[fs.class.path\]](fs.class.path.md) · prev: [29.11.7.5 Iterators \[fs.path.itr\]](fs.path.itr.md) · next: [29.11.7.7 Non-member functions \[fs.path.nonmember\]](fs.path.nonmember.md)

```cpp
template<class charT, class traits>
  friend basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const path& p);
```

**1** *Effects:* Equivalent to `os << quoted(p.string<charT, traits>())`. [*Note 1*: The `quoted` function is described in [[quoted.manip]](quoted.manip.md). — *end note*]

**2** *Returns:* `os`.

```cpp
template<class charT, class traits>
  friend basic_istream<charT, traits>&
    operator>>(basic_istream<charT, traits>& is, path& p);
```

**3** *Effects:* Equivalent to:

```cpp
basic_string<charT, traits> tmp;
is >> quoted(tmp);
p = tmp;
```

**4** *Returns:* `is`.
