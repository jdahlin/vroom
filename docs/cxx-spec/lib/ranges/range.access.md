# 24.3 Range access [range.access]

[Contents](../../README.md) · up: [24 Ranges library \[ranges\]](ranges.md) · prev: [24.2 Header `<ranges>` synopsis \[ranges.syn\]](ranges.syn.md) · next: [24.3.1 `ranges::begin` \[range.access.begin\]](range.access.begin.md)

**1** In addition to being available via inclusion of the `<ranges>` header, the customization point objects in [[range.access]](range.access.md) are available when `<iterator>` ([[iterator.synopsis]](../iterators/iterator.synopsis.md)) is included.

**2** Within this subclause, the *reified object* of a subexpression `E` denotes

- the same object as `E` if `E` is a glvalue, or
- the result of applying the temporary materialization conversion ([[conv.rval]](../../expr/conv.rval.md)) to `E` otherwise.

## Subclauses

- [24.3.1 `ranges::begin` \[range.access.begin\]](range.access.begin.md)
- [24.3.2 `ranges::end` \[range.access.end\]](range.access.end.md)
- [24.3.3 `ranges::cbegin` \[range.access.cbegin\]](range.access.cbegin.md)
- [24.3.4 `ranges::cend` \[range.access.cend\]](range.access.cend.md)
- [24.3.5 `ranges::rbegin` \[range.access.rbegin\]](range.access.rbegin.md)
- [24.3.6 `ranges::rend` \[range.access.rend\]](range.access.rend.md)
- [24.3.7 `ranges::crbegin` \[range.access.crbegin\]](range.access.crbegin.md)
- [24.3.8 `ranges::crend` \[range.access.crend\]](range.access.crend.md)
- [24.3.9 `ranges::size` \[range.prim.size\]](range.prim.size.md)
- [24.3.10 `ranges::ssize` \[range.prim.ssize\]](range.prim.ssize.md)
- [24.3.11 `ranges::empty` \[range.prim.empty\]](range.prim.empty.md)
- [24.3.12 `ranges::data` \[range.prim.data\]](range.prim.data.md)
- [24.3.13 `ranges::cdata` \[range.prim.cdata\]](range.prim.cdata.md)
