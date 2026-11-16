# 29.11.14.35 Status [fs.op.status]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.34 Space \[fs.op.space\]](fs.op.space.md) · next: [29.11.14.36 Status known \[fs.op.status.known\]](fs.op.status.known.md)

```cpp
file_status status(const path& p);
```

**1** *Effects:* As if:

```cpp
error_code ec;
file_status result = status(p, ec);
if (result.type() == file_type::none)
  throw filesystem_error(implementation-supplied-message, p, ec);
return result;
```

**2** *Returns:* See above.

**3** *Throws:* `filesystem_error`. [*Note 1*: `result` values of `file_status(file_type::not_found)` and `file_status(file_type::unknown)` are not considered failures and do not cause an exception to be thrown. — *end note*]

```cpp
file_status status(const path& p, error_code& ec) noexcept;
```

**4** *Effects:* If possible, determines the attributes of the file `p` resolves to, as if by using POSIX `stat()` to obtain a POSIX `struct stat`. If, during attribute determination, the underlying file system API reports an error, sets `ec` to indicate the specific error reported. Otherwise, `ec.clear()`. [*Note 2*: This allows users to inspect the specifics of underlying API errors even when the value returned by `status()` is not `file_status(file_type::none)`. — *end note*]

**5** Let `prms` denote the result of `(m & perms::mask)`, where `m` is determined as if by converting the `st_mode` member of the obtained `struct stat` to the type `perms`.

**6** *Returns:*

- If `ec != error_code()`:

  - If the specific error indicates that `p` cannot be resolved because some element of the path does not exist, returns `file_status(file_type::not_found)`.
  - Otherwise, if the specific error indicates that `p` can be resolved but the attributes cannot be determined, returns `file_status(file_type::unknown)`.
  - Otherwise, returns `file_status(file_type::none)`.

  [*Note 3*: These semantics distinguish between `p` being known not to exist, `p` existing but not being able to determine its attributes, and there being an error that prevents even knowing if `p` exists. These distinctions are important to some use cases. — *end note*]

- Otherwise,

  - If the attributes indicate a regular file, as if by POSIX `S_ISREG`, returns `file_status(file_type::regular, prms)`. [*Note 4*: `file_type::regular` implies appropriate `<fstream>` operations would succeed, assuming no hardware, permission, access, or file system race errors. Lack of `file_type::regular` does not necessarily imply `<fstream>` operations would fail on a directory. — *end note*]
  - Otherwise, if the attributes indicate a directory, as if by POSIX `S_ISDIR`, returns `file_status(file_type::directory, prms)`. [*Note 5*: `file_type::directory` implies that calling `directory_iterator(p)` would succeed. — *end note*]
  - Otherwise, if the attributes indicate a block special file, as if by POSIX `S_ISBLK`, returns `file_status(file_type::block, prms)`.
  - Otherwise, if the attributes indicate a character special file, as if by POSIX `S_ISCHR`, returns `file_status(file_type::character, prms)`.
  - Otherwise, if the attributes indicate a fifo or pipe file, as if by POSIX `S_ISFIFO`, returns `file_status(file_type::fifo, prms)`.
  - Otherwise, if the attributes indicate a socket, as if by POSIX `S_ISSOCK`, returns `file_status(file_type::socket, prms)`.
  - Otherwise, if the attributes indicate an implementation-defined file type ([[fs.enum.file.type]](fs.enum.file.type.md)), returns `file_status(file_type::A, prms)`, where `A` is the constant for the implementation-defined file type.
  - Otherwise, returns `file_status(file_type::unknown, prms)`.

**7** *Remarks:* If a symbolic link is encountered during pathname resolution, pathname resolution continues using the contents of the symbolic link.
