# 28.3.1.4 Operators [locale.operators]

[Contents](../../README.md) · up: [28.3.1 Class `locale` \[locale\]](locale.md) · prev: [28.3.1.3 Members \[locale.members\]](locale.members.md) · next: [28.3.1.5 Static members \[locale.statics\]](locale.statics.md)

```cpp
bool operator==(const locale& other) const;
```

**1** *Returns:* `true` if both arguments are the same locale, or one is a copy of the other, or each has a name and the names are identical; `false` otherwise.

```cpp
template<class charT, class traits, class Allocator>
  bool operator()(const basic_string<charT, traits, Allocator>& s1,
                  const basic_string<charT, traits, Allocator>& s2) const;
```

**2** *Effects:* Compares two strings according to the `collate<charT>` facet.

**3** *Remarks:* This member operator template (and therefore `locale` itself) meets the requirements for a comparator predicate template argument ([[algorithms]](../algorithms/algorithms.md)) applied to strings.

**4** *Returns:*

```cpp
use_facet<collate<charT>>(*this).compare(s1.data(), s1.data() + s1.size(),
                                         s2.data(), s2.data() + s2.size()) < 0
```

**5** [*Example 1*: A vector of strings `v` can be collated according to collation rules in locale `loc` simply by ([[alg.sort]](../algorithms/alg.sort.md), [[vector]](../containers/vector.md)):

```cpp
std::sort(v.begin(), v.end(), loc);
```

— *end example*]
