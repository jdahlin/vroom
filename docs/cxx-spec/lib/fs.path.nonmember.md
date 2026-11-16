# 29.11.7.7 Non-member functions [fs.path.nonmember]

[Contents](../README.md) · up: [29.11.7 Class `path` \[fs.class.path\]](fs.class.path.md) · prev: [29.11.7.6 Inserter and extractor \[fs.path.io\]](fs.path.io.md) · next: [29.11.8 Class `filesystem_error` \[fs.class.filesystem.error\]](fs.class.filesystem.error.md)

```cpp
void swap(path& lhs, path& rhs) noexcept;
```

**1** *Effects:* Equivalent to `lhs.swap(rhs)`.

```cpp
size_t hash_value (const path& p) noexcept;
```

**2** *Returns:* A hash value for the path `p`. If for two paths, `p1 == p2` then `hash_value(p1) == hash_value(p2)`.

```cpp
friend bool operator==(const path& lhs, const path& rhs) noexcept;
```

**3** *Returns:* `lhs.compare(rhs) == 0`.

**4** [*Note 1*: Path equality and path equivalence have different semantics.

- Equality is determined by the `path` non-member `operator==`, which considers the two paths’ lexical representations only. [*Example 1*: `path("foo") == "bar"` is never `true`. — *end example*]
- Equivalence is determined by the `equivalent()` non-member function, which determines if two paths resolve ([[fs.class.path]](fs.class.path.md)) to the same file system entity. [*Example 2*: `equivalent("foo", "bar")` will be `true` when both paths resolve to the same file. — *end example*]

Programmers wishing to determine if two paths are “the same” must decide if “the same” means “the same representation” or “resolve to the same actual file”, and choose the appropriate function accordingly. — *end note*]

```cpp
friend strong_ordering operator<=>(const path& lhs, const path& rhs) noexcept;
```

**5** *Returns:* `lhs.compare(rhs) <=> 0`.

```cpp
friend path operator/ (const path& lhs, const path& rhs);
```

**6** *Effects:* Equivalent to: `return path(lhs) /= rhs;`
