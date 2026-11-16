# 30.12.1.2 Comparisons [re.regiter.comp]

[Contents](../../README.md) · up: [30.12.1 Class template `regex_iterator` \[re.regiter\]](re.regiter.md) · prev: [30.12.1.1 Constructors \[re.regiter.cnstr\]](re.regiter.cnstr.md) · next: [30.12.1.3 Indirection \[re.regiter.deref\]](re.regiter.deref.md)

```cpp
bool operator==(const regex_iterator& right) const;
```

**1** *Returns:* `true` if `*this` and `right` are both end-of-sequence iterators or if the following conditions all hold:

- `begin == right.begin`,
- `end == right.end`,
- `pregex == right.pregex`,
- `flags == right.flags`, and
- `match[0] == right.match[0]`;

otherwise `false`.
