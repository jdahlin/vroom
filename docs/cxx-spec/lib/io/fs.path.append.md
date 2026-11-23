# 29.11.7.4.3 Appends [fs.path.append]

[Contents](../../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.2 Assignments \[fs.path.assign\]](fs.path.assign.md) · next: [29.11.7.4.4 Concatenation \[fs.path.concat\]](fs.path.concat.md)

**1** The append operations use `operator/=` to denote their semantic effect of appending *preferred-separator* when needed.

```cpp
path& operator/=(const path& p);
```

**2** *Effects:* If `p.is_absolute() || (p.has_root_name() && p.root_name() != root_name())`, then `operator=(p)`.

**3** Otherwise, modifies `*this` as if by these steps:

- If `p.has_root_directory()`, then removes any root directory and relative path from the generic format pathname. Otherwise, if `!has_root_directory() && is_absolute()` is `true` or if `has_filename()` is `true`, then appends `path::preferred_separator` to the generic format pathname.
- Then appends the native format pathname of `p`, omitting any *root-name* from its generic format pathname, to the native format pathname.

**4** [*Example 1*: Even if `//host` is interpreted as a *root-name*, both of the paths `path("//host")/"foo"` and `path("//host/")/"foo"` equal `"//host/foo"` (although the former might use backslash as the preferred separator).

Expression examples:

```cpp
// On POSIX,
path("foo") /= path("");        // yields path("foo/")
path("foo") /= path("/bar");    // yields path("/bar")

// On Windows,
path("foo") /= path("");        // yields path("foo\\")
path("foo") /= path("/bar");    // yields path("/bar")
path("foo") /= path("c:/bar");  // yields path("c:/bar")
path("foo") /= path("c:");      // yields path("c:")
path("c:") /= path("");         // yields path("c:")
path("c:foo") /= path("/bar");  // yields path("c:/bar")
path("c:foo") /= path("c:bar"); // yields path("c:foo\\bar")
```

— *end example*]

**5** *Returns:* `*this`.

```cpp
template<class Source>
  path& operator/=(const Source& source);
template<class Source>
  path& append(const Source& source);
```

**6** *Effects:* Equivalent to: `return operator/=(path(source));`

```cpp
template<class InputIterator>
  path& append(InputIterator first, InputIterator last);
```

**7** *Effects:* Equivalent to: `return operator/=(path(first, last));`
