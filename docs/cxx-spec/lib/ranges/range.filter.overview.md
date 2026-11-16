# 24.7.4.1 Overview [range.filter.overview]

[Contents](../../README.md) · up: [24.7.4 Filter view \[range.filter\]](range.filter.md) · prev: [24.7.4 Filter view \[range.filter\]](range.filter.md) · next: [24.7.4.2 Class template `filter_view` \[range.filter.view\]](range.filter.view.md)

**1** `filter_view` presents a `view` of the elements of an underlying sequence that satisfy a predicate.

**2** The name `views::filter` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given subexpressions `E` and `P`, the expression `views::filter(E, P)` is expression-equivalent to `filter_view{E, P}`.

**3** [*Example 1*:

```cpp
vector<int> is{ 0, 1, 2, 3, 4, 5, 6 };
filter_view evens{is, [](int i) { return 0 == i % 2; }};
for (int i : evens)
  cout << i << ' '; // prints: 0 2 4 6
```

— *end example*]
