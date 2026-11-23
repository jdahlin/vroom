# 28.4.2 The numeric category [category.numeric]

[Contents](../../README.md) · up: [28.4 Standard `locale` categories \[locale.categories\]](locale.categories.md) · prev: [28.4.1.5 Class template `codecvt_byname` \[locale.codecvt.byname\]](locale.codecvt.byname.md) · next: [28.4.2.1 Class template `num_get` \[locale.num.get\]](locale.num.get.md)

**1** The classes `num_get<>` and `num_put<>` handle numeric formatting and parsing. Virtual functions are provided for several numeric types. Implementations may (but are not required to) delegate extraction of smaller types to extractors for larger types.[^1]

**2** All specifications of member functions for `num_put` and `num_get` in the subclauses of [[category.numeric]](category.numeric.md) only apply to the specializations required in Tables [[tab:locale.category.facets]](locale.category.md#tab:locale.category.facets) and [[tab:locale.spec]](locale.category.md#tab:locale.spec) ([[locale.category]](locale.category.md)), namely `num_get<char>`, `num_get<wchar_t>`, `num_get<C, InputIterator>`, `num_put<char>`, `num_put<wchar_t>`, and `num_put<C, OutputIterator>`. These specializations refer to the `ios_base&` argument for formatting specifications ([[locale.categories]](locale.categories.md)), and to its imbued locale for the `numpunct<>` facet to identify all numeric punctuation preferences, and also for the `ctype<>` facet to perform character classification.

**3** Extractor and inserter members of the standard iostreams use `num_get<>` and `num_put<>` member functions for formatting and parsing numeric values ([[istream.formatted.reqmts]](../io/istream.formatted.reqmts.md), [[ostream.formatted.reqmts]](../io/ostream.formatted.reqmts.md)).

## Subclauses

- [28.4.2.1 Class template `num_get` \[locale.num.get\]](locale.num.get.md)
  - [28.4.2.1.1 Members \[facet.num.get.members\]](facet.num.get.members.md)
  - [28.4.2.1.2 Virtual functions \[facet.num.get.virtuals\]](facet.num.get.virtuals.md)
- [28.4.2.2 Class template `num_put` \[locale.nm.put\]](locale.nm.put.md)
  - [28.4.2.2.1 Members \[facet.num.put.members\]](facet.num.put.members.md)
  - [28.4.2.2.2 Virtual functions \[facet.num.put.virtuals\]](facet.num.put.virtuals.md)

[^1]: Parsing `"-1"` correctly into, e.g., an `unsigned short` requires that the corresponding member `get()` at least extract the sign before delegating.
