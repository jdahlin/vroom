# 29.11.7.4.9 Decomposition [fs.path.decompose]

[Contents](../../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.8 Compare \[fs.path.compare\]](fs.path.compare.md) · next: [29.11.7.4.10 Query \[fs.path.query\]](fs.path.query.md)

```cpp
path root_name() const;
```

**1** *Returns:* *root-name*, if the pathname in the generic format includes *root-name*, otherwise `path()`.

```cpp
path root_directory() const;
```

**2** *Returns:* *root-directory*, if the pathname in the generic format includes *root-directory*, otherwise `path()`.

```cpp
path root_path() const;
```

**3** *Returns:* `root_name() / root_directory()`.

```cpp
path relative_path() const;
```

**4** *Returns:* A `path` composed from the pathname in the generic format, if `empty()` is `false`, beginning with the first *filename* after `root_path()`. Otherwise, `path()`.

```cpp
path parent_path() const;
```

**5** *Returns:* `*this` if `has_relative_path()` is `false`, otherwise a path whose generic format pathname is the longest prefix of the generic format pathname of `*this` that produces one fewer element in its iteration.

```cpp
path filename() const;
```

**6** *Returns:* `relative_path().empty() ? path(): *--end()`.

**7** [*Example 1*:

```cpp
path("/foo/bar.txt").filename();        // yields "bar.txt"
path("/foo/bar").filename();            // yields "bar"
path("/foo/bar/").filename();           // yields ""
path("/").filename();                   // yields ""
path("//host").filename(); // yields ""
path(".").filename();                   // yields "."
path("..").filename();                  // yields ".."
```

— *end example*]

```cpp
path stem() const;
```

**8** *Returns:* Let `f` be the generic format pathname of `filename()`. Returns a path whose pathname in the generic format is

- `f`, if it contains no periods other than a leading period or consists solely of one or two periods;
- otherwise, the prefix of `f` ending before its last period.

**9** [*Example 2*:

```cpp
std::cout << path("/foo/bar.txt").stem();       // outputs "bar"
path p = "foo.bar.baz.tar";
for (; !p.extension().empty(); p = p.stem())
  std::cout << p.extension() << '\n';
  // outputs: .tar
  //          .baz
  //          .bar
```

— *end example*]

```cpp
path extension() const;
```

**10** *Returns:* A path whose pathname in the generic format is the suffix of `filename()` not included in `stem()`.

**11** [*Example 3*:

```cpp
path("/foo/bar.txt").extension();       // yields ".txt" and stem() is "bar"
path("/foo/bar").extension();           // yields "" and stem() is "bar"
path("/foo/.profile").extension();      // yields "" and stem() is ".profile"
path(".bar").extension();               // yields "" and stem() is ".bar"
path("..bar").extension();              // yields ".bar" and stem() is "."
```

— *end example*]

**12** [*Note 1*: The period is included in the return value so that it is possible to distinguish between no extension and an empty extension. — *end note*]

**13** [*Note 2*: On non-POSIX operating systems, for a path `p`, it may not be the case that `p.stem() + p.extension() == p.filename()`, even though the generic format pathnames are the same. — *end note*]
