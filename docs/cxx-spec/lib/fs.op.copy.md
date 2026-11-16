# 29.11.14.3 Copy [fs.op.copy]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.2 Canonical \[fs.op.canonical\]](fs.op.canonical.md) · next: [29.11.14.4 Copy file \[fs.op.copy.file\]](fs.op.copy.file.md)

```cpp
void copy(const path& from, const path& to);
```

**1** *Effects:* Equivalent to `copy(from, to, copy_options::none)`.

```cpp
void copy(const path& from, const path& to, error_code& ec);
```

**2** *Effects:* Equivalent to `copy(from, to, copy_options::none, ec)`.

```cpp
void copy(const path& from, const path& to, copy_options options);
void copy(const path& from, const path& to, copy_options options,
          error_code& ec);
```

**3** *Preconditions:* At most one element from each option group ([[fs.enum.copy.opts]](fs.enum.copy.opts.md)) is set in `options`.

**4** *Effects:* Before the first use of `f` and `t`:

- If

  ```cpp
  (options & copy_options::create_symlinks) != copy_options::none ||
  (options & copy_options::skip_symlinks) != copy_options::none
  ```

  then `auto f = symlink_status(from)` and if needed `auto t = symlink_status(to)`.

- Otherwise, if

  ```cpp
  (options & copy_options::copy_symlinks) != copy_options::none
  ```

  then `auto f = symlink_status(from)` and if needed `auto t = status(to)`.

- Otherwise, `auto f = status(from)` and if needed `auto t = status(to)`.

Effects are then as follows:

- If `f.type()` or `t.type()` is an implementation-defined file type ([[fs.enum.file.type]](fs.enum.file.type.md)), then the effects are implementation-defined.

- Otherwise, an error is reported as specified in [[fs.err.report]](fs.err.report.md) if:

  - `exists(f)` is `false`, or
  - `equivalent(from, to)` is `true`, or
  - `is_other(f) || is_other(t)` is `true`, or
  - `is_directory(f) && is_regular_file(t)` is `true`.

- Otherwise, if `is_symlink(f)`, then:

  - If `(options & copy_options::skip_symlinks) != copy_options::none` then return.

  - Otherwise if

    ```cpp
    !exists(t) && (options & copy_options::copy_symlinks) != copy_options::none
    ```

    then `copy_symlink(from, to)`.

  - Otherwise report an error as specified in [[fs.err.report]](fs.err.report.md).

- Otherwise, if `is_regular_file(f)`, then:

  - If `(options & copy_options::directories_only) != copy_options::none`, then return.
  - Otherwise, if `(options & copy_options::create_symlinks) != copy_options::none`, then create a symbolic link to the source file.
  - Otherwise, if `(options & copy_options::create_hard_links) != copy_options::none`, then create a hard link to the source file.
  - Otherwise, if `is_directory(t)`, then `copy_file(from, to/from.filename(), options)`.
  - Otherwise, `copy_file(from, to, options)`.

- Otherwise, if

  ```cpp
  is_directory(f) &&
  (options & copy_options::create_symlinks) != copy_options::none
  ```

  then report an error with an `error_code` argument equal to `make_error_code(errc::is_a_directory)`.

- Otherwise, if

  ```cpp
  is_directory(f) &&
  ((options & copy_options::recursive) != copy_options::none ||
   options == copy_options::none)
  ```

  then:

  - If `exists(t)` is `false`, then `create_directory(to, from)`.

  - Then, iterate over the files in `from`, as if by

    ```cpp
    for (const directory_entry& x : directory_iterator(from))
      copy(x.path(), to/x.path().filename(),
           options | copy_options::in-recursive-copy);
    ```

    where `in-recursive-copy` is a bitmask element of `copy_options` that is not one of the elements in [[fs.enum.copy.opts]](fs.enum.copy.opts.md).

- Otherwise, for the signature with argument `ec`, `ec.clear()`.

- Otherwise, no effects.

**5** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**6** *Remarks:* For the signature with argument `ec`, any library functions called by the implementation shall have an `error_code` argument if applicable.

**7** [*Example 1*: Given this directory structure:

```cpp
/dir1
  file1
  file2
  dir2
    file3
```

Calling `copy("/dir1", "/dir3")` would result in:

```cpp
/dir1
  file1
  file2
  dir2
    file3
/dir3
  file1
  file2
```

Alternatively, calling `copy("/dir1", "/dir3", copy_options::recursive)` would result in:

```cpp
/dir1
  file1
  file2
  dir2
    file3
/dir3
  file1
  file2
  dir2
    file3
```

— *end example*]
