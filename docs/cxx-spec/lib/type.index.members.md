# 20.17.3 `type_index` members [type.index.members]

[Contents](../README.md) · up: [20.17 Class `type_index` \[type.index\]](type.index.md) · prev: [20.17.2 `type_index` overview \[type.index.overview\]](type.index.overview.md) · next: [20.17.4 Hash support \[type.index.hash\]](type.index.hash.md)

```cpp
type_index(const type_info& rhs) noexcept;
```

**1** *Effects:* Constructs a `type_index` object, the equivalent of `target = &rhs`.

```cpp
bool operator==(const type_index& rhs) const noexcept;
```

**2** *Returns:* `*target == *rhs.target`.

```cpp
bool operator<(const type_index& rhs) const noexcept;
```

**3** *Returns:* `target->before(*rhs.target)`.

```cpp
bool operator>(const type_index& rhs) const noexcept;
```

**4** *Returns:* `rhs.target->before(*target)`.

```cpp
bool operator<=(const type_index& rhs) const noexcept;
```

**5** *Returns:* `!rhs.target->before(*target)`.

```cpp
bool operator>=(const type_index& rhs) const noexcept;
```

**6** *Returns:* `!target->before(*rhs.target)`.

```cpp
strong_ordering operator<=>(const type_index& rhs) const noexcept;
```

**7** *Effects:* Equivalent to:

```cpp
if (*target == *rhs.target) return strong_ordering::equal;
if (target->before(*rhs.target)) return strong_ordering::less;
return strong_ordering::greater;
```

```cpp
size_t hash_code() const noexcept;
```

**8** *Returns:* `target->hash_code()`.

```cpp
const char* name() const noexcept;
```

**9** *Returns:* `target->name()`.
