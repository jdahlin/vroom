# 29.11.7.4.8 Compare [fs.path.compare]

[Contents](../README.md) · up: [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md) · prev: [29.11.7.4.7 Generic format observers \[fs.path.generic.obs\]](fs.path.generic.obs.md) · next: [29.11.7.4.9 Decomposition \[fs.path.decompose\]](fs.path.decompose.md)

```cpp
int compare(const path& p) const noexcept;
```

**1** *Returns:*

- Let `rootNameComparison` be the result of `this->root_name().native().compare(p.root_name().native())`. If `rootNameComparison` is not `0`, `rootNameComparison`.
- Otherwise, if `!this->has_root_directory()` and `p.has_root_directory()`, a value less than `0`.
- Otherwise, if `this->has_root_directory()` and `!p.has_root_directory()`, a value greater than `0`.
- Otherwise, if `native()` for the elements of `this->relative_path()` are lexicographically less than `native()` for the elements of `p.relative_path()`, a value less than `0`.
- Otherwise, if `native()` for the elements of `this->relative_path()` are lexicographically greater than `native()` for the elements of `p.relative_path()`, a value greater than `0`.
- Otherwise, `0`.

```cpp
int compare(const string_type& s) const;
int compare(basic_string_view<value_type> s) const;
int compare(const value_type* s) const;
```

**2** *Effects:* Equivalent to: `return compare(path(s));`
