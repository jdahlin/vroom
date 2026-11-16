# 29.11.7.4.5 Modifiers [fs.path.modifiers]

[Contents](../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.4 Concatenation \[fs.path.concat\]](fs.path.concat.md) · next: [29.11.7.4.6 Native format observers \[fs.path.native.obs\]](fs.path.native.obs.md)

```cpp
void clear() noexcept;
```

**1** *Postconditions:* `empty() == true`.

```cpp
path& make_preferred();
```

**2** *Effects:* Each *directory-separator* of the pathname in the generic format is converted to *preferred-separator*.

**3** *Returns:* `*this`.

**4** [*Example 1*:

```cpp
path p("foo/bar");
std::cout << p << '\n';
p.make_preferred();
std::cout << p << '\n';
```

On an operating system where *preferred-separator* is a slash, the output is:

```cpp
"foo/bar"
"foo/bar"
```

On an operating system where *preferred-separator* is a backslash, the output is:

```cpp
"foo/bar"
"foo\bar"
```

— *end example*]

```cpp
path& remove_filename();
```

**5** *Effects:* Remove the generic format pathname of `filename()` from the generic format pathname.

**6** *Postconditions:* `!has_filename()`.

**7** *Returns:* `*this`.

**8** [*Example 2*:

```cpp
path("foo/bar").remove_filename();      // yields "foo/"
path("foo/").remove_filename();         // yields "foo/"
path("/foo").remove_filename();         // yields "/"
path("/").remove_filename();            // yields "/"
```

— *end example*]

```cpp
path& replace_filename(const path& replacement);
```

**9** *Effects:* Equivalent to:

```cpp
remove_filename();
operator/=(replacement);
```

**10** *Returns:* `*this`.

**11** [*Example 3*:

```cpp
path("/foo").replace_filename("bar");   // yields "/bar" on POSIX
path("/").replace_filename("bar");      // yields "/bar" on POSIX
```

— *end example*]

```cpp
path& replace_extension(const path& replacement = path());
```

**12** *Effects:*

- Any existing `extension()` ([[fs.path.decompose]](fs.path.decompose.md)) is removed from the pathname in the generic format, then
- If `replacement` is not empty and does not begin with a dot character, a dot character is appended to the pathname in the generic format, then
- `operator+=(replacement);`.

**13** *Returns:* `*this`.

```cpp
void swap(path& rhs) noexcept;
```

**14** *Effects:* Swaps the contents (in all formats) of the two paths.

**15** *Complexity:* Constant time.
