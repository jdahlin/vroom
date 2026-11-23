# 28.4.4.1.1 Members [locale.collate.members]

[Contents](../../README.md) · up: [28.4.4.1 Class template `collate` \[locale.collate\]](locale.collate.md) · prev: [28.4.4.1 Class template `collate` \[locale.collate\]](locale.collate.md) · next: [28.4.4.1.2 Virtual functions \[locale.collate.virtuals\]](locale.collate.virtuals.md)

```cpp
int compare(const charT* low1, const charT* high1,
            const charT* low2, const charT* high2) const;
```

**1** *Returns:* `do_compare(low1, high1, low2, high2)`.

```cpp
string_type transform(const charT* low, const charT* high) const;
```

**2** *Returns:* `do_transform(low, high)`.

```cpp
long hash(const charT* low, const charT* high) const;
```

**3** *Returns:* `do_hash(low, high)`.
