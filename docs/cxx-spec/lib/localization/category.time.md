# 28.4.5 The time category [category.time]

[Contents](../../README.md) · up: [28.4 Standard `locale` categories \[locale.categories\]](locale.categories.md) · prev: [28.4.4.2 Class template `collate_byname` \[locale.collate.byname\]](locale.collate.byname.md) · next: [28.4.5.1 Class template `time_get` \[locale.time.get\]](locale.time.get.md)

**1** Templates `time_get<charT, InputIterator>` and `time_put<charT, OutputIterator>` provide date and time formatting and parsing. All specifications of member functions for `time_put` and `time_get` in the subclauses of [[category.time]](category.time.md) only apply to the specializations required in Tables [[tab:locale.category.facets]](locale.category.md#tab:locale.category.facets) and [[tab:locale.spec]](locale.category.md#tab:locale.spec) ([[locale.category]](locale.category.md)). Their members use their `ios_base&`, `ios_base::iostate&`, and `fill` arguments as described in [[locale.categories]](locale.categories.md), and the `ctype<>` facet, to determine formatting details.

## Subclauses

- [28.4.5.1 Class template `time_get` \[locale.time.get\]](locale.time.get.md)
  - [28.4.5.1.1 Members \[locale.time.get.members\]](locale.time.get.members.md)
  - [28.4.5.1.2 Virtual functions \[locale.time.get.virtuals\]](locale.time.get.virtuals.md)
- [28.4.5.2 Class template `time_get_byname` \[locale.time.get.byname\]](locale.time.get.byname.md)
- [28.4.5.3 Class template `time_put` \[locale.time.put\]](locale.time.put.md)
  - [28.4.5.3.1 Members \[locale.time.put.members\]](locale.time.put.members.md)
  - [28.4.5.3.2 Virtual functions \[locale.time.put.virtuals\]](locale.time.put.virtuals.md)
- [28.4.5.4 Class template `time_put_byname` \[locale.time.put.byname\]](locale.time.put.byname.md)
