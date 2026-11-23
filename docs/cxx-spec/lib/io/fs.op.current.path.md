# 29.11.14.11 Current path [fs.op.current.path]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.10 Create symlink \[fs.op.create.symlink\]](fs.op.create.symlink.md) · next: [29.11.14.12 Equivalent \[fs.op.equivalent\]](fs.op.equivalent.md)

```cpp
path current_path();
path current_path(error_code& ec);
```

**1** *Returns:* The absolute path of the current working directory, whose pathname in the native format is obtained as if by POSIX `getcwd()`. The signature with argument `ec` returns `path()` if an error occurs.

**2** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**3** *Remarks:* The current working directory is the directory, associated with the process, that is used as the starting location in pathname resolution for relative paths.

**4** [*Note 1*: The `current_path()` name was chosen to emphasize that the returned value is a path, not just a single directory name. — *end note*]

**5** [*Note 2*: The current path as returned by many operating systems is a dangerous global variable. It may be changed unexpectedly by third-party or system library functions, or by another thread. — *end note*]

```cpp
void current_path(const path& p);
void current_path(const path& p, error_code& ec) noexcept;
```

**6** *Effects:* Establishes the postcondition, as if by POSIX `chdir()`.

**7** *Postconditions:* `equivalent(p, current_path())`.

**8** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).

**9** [*Note 3*: The current path for many operating systems is a dangerous global state. It may be changed unexpectedly by a third-party or system library functions, or by another thread. — *end note*]
