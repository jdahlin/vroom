# 29.11.6 Error reporting [fs.err.report]

[Contents](../../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.5 Header `<filesystem>` synopsis \[fs.filesystem.syn\]](fs.filesystem.syn.md) · next: [29.11.7 Class `path` \[fs.class.path\]](fs.class.path.md)

**1** Filesystem library functions often provide two overloads, one that throws an exception to report file system errors, and another that sets an `error_code`. [*Note 1*: This supports two common use cases:

- Uses where file system errors are truly exceptional and indicate a serious failure. Throwing an exception is an appropriate response.
- Uses where file system errors are routine and do not necessarily represent failure. Returning an error code is the most appropriate response. This allows application specific error handling, including simply ignoring the error.

— *end note*]

**2** Functions not having an argument of type `error_code&` handle errors as follows, unless otherwise specified:

- When a call by the implementation to an operating system or other underlying API results in an error that prevents the function from meeting its specifications, an exception of type `filesystem_error` shall be thrown. For functions with a single path argument, that argument shall be passed to the `filesystem_error` constructor with a single path argument. For functions with two path arguments, the first of these arguments shall be passed to the `filesystem_error` constructor as the `path1` argument, and the second shall be passed as the `path2` argument. The `filesystem_error` constructor’s `error_code` argument is set as appropriate for the specific operating system dependent error.
- Failure to allocate storage is reported by throwing an exception as described in [[res.on.exception.handling]](../library/res.on.exception.handling.md).
- Destructors throw nothing.

**3** Functions having an argument of type `error_code&` handle errors as follows, unless otherwise specified:

- If a call by the implementation to an operating system or other underlying API results in an error that prevents the function from meeting its specifications, the `error_code&` argument is set as appropriate for the specific operating system dependent error. Otherwise, `clear()` is called on the `error_code&` argument.
