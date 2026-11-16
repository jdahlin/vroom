# 29.11.7.4.11 Generation [fs.path.gen]

[Contents](../../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.10 Query \[fs.path.query\]](fs.path.query.md) · next: [29.11.7.5 Iterators \[fs.path.itr\]](fs.path.itr.md)

```cpp
path lexically_normal() const;
```

**1** *Returns:* A path whose pathname in the generic format is the normal form ([[fs.path.generic]](fs.path.generic.md)) of the pathname in the generic format of `*this`.

**2** [*Example 1*:

```cpp
assert(path("foo/./bar/..").lexically_normal() == "foo/");
assert(path("foo/.///bar/../").lexically_normal() == "foo/");
```

The above assertions will succeed. On Windows, the returned path’s *directory-separator* characters will be backslashes rather than slashes, but that does not affect `path` equality. — *end example*]

```cpp
path lexically_relative(const path& base) const;
```

**3** *Returns:* `*this` made relative to `base`. Does not resolve ([[fs.class.path]](fs.class.path.md)) symlinks. Does not first normalize ([[fs.path.generic]](fs.path.generic.md)) `*this` or `base`.

**4** *Effects:* If:

- `root_name() != base.root_name()` is `true`, or
- `is_absolute() != base.is_absolute()` is `true`, or
- `!has_root_directory() && base.has_root_directory()` is `true`, or
- any *filename* in `relative_path()` or `base.relative_path()` can be interpreted as a *root-name*,

returns `path()`. [*Note 1*: On a POSIX implementation, no *filename* in a *relative-path* is acceptable as a *root-name*. — *end note*] Determines the first mismatched element of `*this` and `base` as if by:

```cpp
auto [a, b] = mismatch(begin(), end(), base.begin(), base.end());
```

Then,

- if `a == end()` and `b == base.end()`, returns `path(".")`; otherwise

- let `n` be the number of *filename* elements in `[b, base.end())` that are not dot or dot-dot or empty, minus the number that are dot-dot. If `n<0,` returns `path()`; otherwise

- if `n == 0` and `(a == end() || a->empty())`, returns `path(".")`; otherwise

- returns an object of class `path` that is default-constructed, followed by

  - application of `operator/=(path(".."))` `n` times, and then
  - application of `operator/=` for each element in `[a, end())`.

**5** [*Example 2*:

```cpp
assert(path("/a/d").lexically_relative("/a/b/c") == "../../d");
assert(path("/a/b/c").lexically_relative("/a/d") == "../b/c");
assert(path("a/b/c").lexically_relative("a") == "b/c");
assert(path("a/b/c").lexically_relative("a/b/c/x/y") == "../..");
assert(path("a/b/c").lexically_relative("a/b/c") == ".");
assert(path("a/b").lexically_relative("c/d") == "../../a/b");
```

The above assertions will succeed. On Windows, the returned path’s *directory-separator* characters will be backslashes rather than slashes, but that does not affect `path` equality. — *end example*]

**6** [*Note 2*: If symlink following semantics are desired, use the operational function `relative()`. — *end note*]

**7** [*Note 3*: If normalization ([[fs.path.generic]](fs.path.generic.md)) is needed to ensure consistent matching of elements, apply `lexically_normal()` to `*this`, `base`, or both. — *end note*]

```cpp
path lexically_proximate(const path& base) const;
```

**8** *Returns:* If the value of `lexically_relative(base)` is not an empty path, return it. Otherwise return `*this`.

**9** [*Note 4*: If symlink following semantics are desired, use the operational function `proximate()`. — *end note*]

**10** [*Note 5*: If normalization ([[fs.path.generic]](fs.path.generic.md)) is needed to ensure consistent matching of elements, apply `lexically_normal()` to `*this`, `base`, or both. — *end note*]
