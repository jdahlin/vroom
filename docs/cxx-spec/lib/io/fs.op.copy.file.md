# 29.11.14.4 Copy file [fs.op.copy.file]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.3 Copy \[fs.op.copy\]](fs.op.copy.md) · next: [29.11.14.5 Copy symlink \[fs.op.copy.symlink\]](fs.op.copy.symlink.md)

```cpp
bool copy_file(const path& from, const path& to);
bool copy_file(const path& from, const path& to, error_code& ec);
```

**1** *Returns:* `copy_file(from, to, copy_options::none)` or<br> `copy_file(from, to, copy_options::none, ec)`, respectively.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

```cpp
bool copy_file(const path& from, const path& to, copy_options options);
bool copy_file(const path& from, const path& to, copy_options options,
               error_code& ec);
```

**3** *Preconditions:* At most one element from each option group ([[fs.enum.copy.opts]](fs.enum.copy.opts.md)) is set in `options`.

**4** *Effects:* As follows:

- Report an error as specified in [[fs.err.report]](fs.err.report.md) if:

  - `is_regular_file(from)` is `false`, or

  - `exists(to)` is `true` and `is_regular_file(to)` is `false`, or

  - `exists(to)` is `true` and `equivalent(from, to)` is `true`, or

  - `exists(to)` is `true` and

    ```cpp
    (options & (copy_options::skip_existing |
                copy_options::overwrite_existing |
                copy_options::update_existing)) == copy_options::none
    ```

- Otherwise, copy the contents and attributes of the file `from` resolves to, to the file `to` resolves to, if:

  - `exists(to)` is `false`, or
  - `(options & copy_options::overwrite_existing) != copy_options::none`, or
  - `(options & copy_options::update_existing) != copy_options::none` and `from` is more recent than `to`, determined as if by use of the `last_write_time` function ([[fs.op.last.write.time]](fs.op.last.write.time.md)).

- Otherwise, no effects.

**5** *Returns:* `true` if the `from` file was copied, otherwise `false`. The signature with argument `ec` returns `false` if an error occurs.

**6** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**7** *Complexity:* At most one direct or indirect invocation of `status(to)`.
