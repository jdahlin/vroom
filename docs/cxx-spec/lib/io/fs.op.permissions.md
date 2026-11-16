# 29.11.14.26 Permissions [fs.op.permissions]

[Contents](../../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.25 Last write time \[fs.op.last.write.time\]](fs.op.last.write.time.md) · next: [29.11.14.27 Proximate \[fs.op.proximate\]](fs.op.proximate.md)

```cpp
void permissions(const path& p, perms prms, perm_options opts=perm_options::replace);
void permissions(const path& p, perms prms, error_code& ec) noexcept;
void permissions(const path& p, perms prms, perm_options opts, error_code& ec);
```

**1** *Preconditions:* Exactly one of the `perm_options` constants `replace`, `add`, or `remove` is present in `opts`.

**2** *Remarks:* The second signature behaves as if it had an additional parameter `perm_options` `opts` with an argument of `perm_options::replace`.

**3** *Effects:* Applies the action specified by `opts` to the file `p` resolves to, or to file `p` itself if `p` is a symbolic link and `perm_options::nofollow` is set in `opts`. The action is applied as if by POSIX `fchmodat()`.

**4** [*Note 1*: Conceptually permissions are viewed as bits, but the actual implementation may use some other mechanism. — *end note*]

**5** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
