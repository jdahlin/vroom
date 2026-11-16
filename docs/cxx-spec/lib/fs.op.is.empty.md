# 29.11.14.19 Is empty [fs.op.is.empty]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.18 Is directory \[fs.op.is.directory\]](fs.op.is.directory.md) · next: [29.11.14.20 Is fifo \[fs.op.is.fifo\]](fs.op.is.fifo.md)

```cpp
bool is_empty(const path& p);
bool is_empty(const path& p, error_code& ec);
```

**1** *Effects:*

- Determine `file_status s`, as if by `status(p)` or `status(p, ec)`, respectively.

- For the signature with argument `ec`, return `false` if an error occurred.

- Otherwise, if `is_directory(s)`:

  - Create a variable `itr`, as if by `directory_iterator itr(p)` or `directory_iterator itr(p, ec)`, respectively.
  - For the signature with argument `ec`, return `false` if an error occurred.
  - Otherwise, return `itr == directory_iterator()`.

- Otherwise:

  - Determine `uintmax_t sz`, as if by `file_size(p)` or `file_size(p, ec)`, respectively.
  - For the signature with argument `ec`, return `false` if an error occurred.
  - Otherwise, return `sz == 0`.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
