# 24.7.5.1 Overview [range.transform.overview]

[Contents](../README.md) · up: [24.7.5 Transform view \[range.transform\]](range.transform.md) · prev: [24.7.5 Transform view \[range.transform\]](range.transform.md) · next: [24.7.5.2 Class template `transform_view` \[range.transform.view\]](range.transform.view.md)

**1** `transform_view` presents a `view` of an underlying sequence after applying a transformation function to each element.

**2** The name `views::transform` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given subexpressions `E` and `F`, the expression `views::transform(E, F)` is expression-equivalent to `transform_view{E, F}`.

**3** [*Example 1*:

```cpp
vector<int> is{ 0, 1, 2, 3, 4 };
transform_view squares{is, [](int i) { return i * i; }};
for (int i : squares)
  cout << i << ' '; // prints: 0 1 4 9 16
```

— *end example*]
