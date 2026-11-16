# 29.11.14.39 Weakly canonical [fs.op.weakly.canonical]

[Contents](../README.md) · up: [29.11.14 Filesystem operation functions \[fs.op.funcs\]](fs.op.funcs.md) · prev: [29.11.14.38 Temporary directory path \[fs.op.temp.dir.path\]](fs.op.temp.dir.path.md) · next: [29.12 C library files \[c.files\]](c.files.md)

```cpp
path weakly_canonical(const path& p);
path weakly_canonical(const path& p, error_code& ec);
```

**1** *Returns:* `p` with symlinks resolved and the result normalized ([[fs.path.generic]](fs.path.generic.md)).

**2** *Effects:* Using `status(p)` or `status(p, ec)`, respectively, to determine existence, return a path composed by `operator/=` from the result of calling `canonical()` with a path argument composed of the leading elements of `p` that exist, if any, followed by the elements of `p` that do not exist, if any. For the first form, `canonical()` is called without an `error_code` argument. For the second form, `canonical()` is called with `ec` as an `error_code` argument, and `path()` is returned at the first error occurrence, if any.

**3** *Postconditions:* The returned path is in normal form ([[fs.path.generic]](fs.path.generic.md)).

**4** *Remarks:* Implementations should avoid unnecessary normalization such as when `canonical` has already been called on the entirety of `p`.

**5** *Throws:* As specified in [[fs.err.report]](fs.err.report.md).
