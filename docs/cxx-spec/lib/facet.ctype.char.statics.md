# 28.4.1.3.3 Static members [facet.ctype.char.statics]

[Contents](../README.md) · up: [28.4.1.3 `ctype<char>` specialization \[facet.ctype.special\]](facet.ctype.special.md) · prev: [28.4.1.3.2 Members \[facet.ctype.char.members\]](facet.ctype.char.members.md) · next: [28.4.1.3.4 Virtual functions \[facet.ctype.char.virtuals\]](facet.ctype.char.virtuals.md)

```cpp
static const mask* classic_table() noexcept;
```

**1** *Returns:* A pointer to the initial element of an array of size `table_size` which represents the classifications of characters in the `"C"` locale.
