# 29.11.14.38 Temporary directory path [fs.op.temp.dir.path]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.37 Symlink status \[fs.op.symlink.status\]](fs.op.symlink.status.md) · next: [29.11.14.39 Weakly canonical \[fs.op.weakly.canonical\]](fs.op.weakly.canonical.md)

```cpp
path temp_directory_path();
path temp_directory_path(error_code& ec);
```

**1** Let `p` be an unspecified directory path suitable for temporary files.

**2** *Effects:* If `exists(p)` is `false` or `is_directory(p)` is `false`, an error is reported ([[fs.err.report]](fs.err.report.md)).

**3** *Returns:* The path `p`. The signature with argument `ec` returns `path()` if an error occurs.

**4** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**5** [*Example 1*: For POSIX-based operating systems, an implementation might return the path supplied by the first environment variable found in the list TMPDIR, TMP, TEMP, TEMPDIR, or if none of these are found, `"/tmp"`.

For Windows-based operating systems, an implementation might return the path reported by the Windows `GetTempPath` API function. — *end example*]
