# 24.7 Range adaptors [range.adaptors]

[Contents](../../README.md) · up: [24 Ranges library \[ranges\]](ranges.md) · prev: [24.6.4.3 Class template `basic_istream_view::iterator` \[range.istream.iterator\]](range.istream.iterator.md) · next: [24.7.1 Range adaptor objects \[range.adaptor.object\]](range.adaptor.object.md)

**1** This subclause defines *range adaptors*, which are utilities that transform a `range` into a `view` with custom behaviors. These adaptors can be chained to create pipelines of range transformations that evaluate lazily as the resulting view is iterated.

**2** Range adaptors are declared in namespace `std::ranges::views`.

**3** The bitwise OR operator is overloaded for the purpose of creating adaptor chain pipelines. The adaptors also support function call syntax with equivalent semantics.

**4** [*Example 1*:

```cpp
vector<int> ints{0,1,2,3,4,5};
auto even = [](int i){ return 0 == i % 2; };
auto square = [](int i) { return i * i; };
for (int i : ints | views::filter(even) | views::transform(square)) {
  cout << i << ' '; // prints: 0 4 16
}
assert(ranges::equal(ints | views::filter(even), views::filter(ints, even)));
```

— *end example*]

## Subclauses

- [24.7.1 Range adaptor objects \[range.adaptor.object\]](range.adaptor.object.md)
- [24.7.2 Semiregular wrapper \[range.semi.wrap\]](range.semi.wrap.md)
- [24.7.3 All view \[range.all\]](range.all.md)
  - [24.7.3.1 Class template `ref_view` \[range.ref.view\]](range.ref.view.md)
- [24.7.4 Filter view \[range.filter\]](range.filter.md)
  - [24.7.4.1 Overview \[range.filter.overview\]](range.filter.overview.md)
  - [24.7.4.2 Class template `filter_view` \[range.filter.view\]](range.filter.view.md)
  - [24.7.4.3 Class `filter_view::iterator` \[range.filter.iterator\]](range.filter.iterator.md)
  - [24.7.4.4 Class `filter_view::sentinel` \[range.filter.sentinel\]](range.filter.sentinel.md)
- [24.7.5 Transform view \[range.transform\]](range.transform.md)
  - [24.7.5.1 Overview \[range.transform.overview\]](range.transform.overview.md)
  - [24.7.5.2 Class template `transform_view` \[range.transform.view\]](range.transform.view.md)
  - [24.7.5.3 Class template `transform_view::iterator` \[range.transform.iterator\]](range.transform.iterator.md)
  - [24.7.5.4 Class template `transform_view::sentinel` \[range.transform.sentinel\]](range.transform.sentinel.md)
- [24.7.6 Take view \[range.take\]](range.take.md)
  - [24.7.6.1 Overview \[range.take.overview\]](range.take.overview.md)
  - [24.7.6.2 Class template `take_view` \[range.take.view\]](range.take.view.md)
  - [24.7.6.3 Class template `take_view::sentinel` \[range.take.sentinel\]](range.take.sentinel.md)
- [24.7.7 Take while view \[range.take.while\]](range.take.while.md)
  - [24.7.7.1 Overview \[range.take.while.overview\]](range.take.while.overview.md)
  - [24.7.7.2 Class template `take_while_view` \[range.take.while.view\]](range.take.while.view.md)
  - [24.7.7.3 Class template `take_while_view::sentinel` \[range.take.while.sentinel\]](range.take.while.sentinel.md)
- [24.7.8 Drop view \[range.drop\]](range.drop.md)
  - [24.7.8.1 Overview \[range.drop.overview\]](range.drop.overview.md)
  - [24.7.8.2 Class template `drop_view` \[range.drop.view\]](range.drop.view.md)
- [24.7.9 Drop while view \[range.drop.while\]](range.drop.while.md)
  - [24.7.9.1 Overview \[range.drop.while.overview\]](range.drop.while.overview.md)
  - [24.7.9.2 Class template `drop_while_view` \[range.drop.while.view\]](range.drop.while.view.md)
- [24.7.10 Join view \[range.join\]](range.join.md)
  - [24.7.10.1 Overview \[range.join.overview\]](range.join.overview.md)
  - [24.7.10.2 Class template `join_view` \[range.join.view\]](range.join.view.md)
  - [24.7.10.3 Class template `join_view::iterator` \[range.join.iterator\]](range.join.iterator.md)
  - [24.7.10.4 Class template `join_view::sentinel` \[range.join.sentinel\]](range.join.sentinel.md)
- [24.7.11 Split view \[range.split\]](range.split.md)
  - [24.7.11.1 Overview \[range.split.overview\]](range.split.overview.md)
  - [24.7.11.2 Class template `split_view` \[range.split.view\]](range.split.view.md)
  - [24.7.11.3 Class template `split_view::outer-iterator` \[range.split.outer\]](range.split.outer.md)
  - [24.7.11.4 Class `split_view::outer-iterator::value_type` \[range.split.outer.value\]](range.split.outer.value.md)
  - [24.7.11.5 Class template `split_view::inner-iterator` \[range.split.inner\]](range.split.inner.md)
- [24.7.12 Counted view \[range.counted\]](range.counted.md)
- [24.7.13 Common view \[range.common\]](range.common.md)
  - [24.7.13.1 Overview \[range.common.overview\]](range.common.overview.md)
  - [24.7.13.2 Class template `common_view` \[range.common.view\]](range.common.view.md)
- [24.7.14 Reverse view \[range.reverse\]](range.reverse.md)
  - [24.7.14.1 Overview \[range.reverse.overview\]](range.reverse.overview.md)
  - [24.7.14.2 Class template `reverse_view` \[range.reverse.view\]](range.reverse.view.md)
- [24.7.15 Elements view \[range.elements\]](range.elements.md)
  - [24.7.15.1 Overview \[range.elements.overview\]](range.elements.overview.md)
  - [24.7.15.2 Class template `elements_view` \[range.elements.view\]](range.elements.view.md)
  - [24.7.15.3 Class template `elements_view::iterator` \[range.elements.iterator\]](range.elements.iterator.md)
  - [24.7.15.4 Class template `elements_view::sentinel` \[range.elements.sentinel\]](range.elements.sentinel.md)
