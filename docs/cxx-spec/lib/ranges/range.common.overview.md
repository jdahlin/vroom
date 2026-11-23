# 24.7.13.1 Overview [range.common.overview]

[Contents](../../README.md) · up: [24.7.13 Common view \[range.common\]](range.common.md) · prev: [24.7.13 Common view \[range.common\]](range.common.md) · next: [24.7.13.2 Class template `common_view` \[range.common.view\]](range.common.view.md)

**1** `common_view` takes a `view` which has different types for its iterator and sentinel and turns it into a `view` of the same elements with an iterator and sentinel of the same type.

**2** [*Note 1*: `common_view` is useful for calling legacy algorithms that expect a range’s iterator and sentinel types to be the same. — *end note*]

**3** The name `views::common` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given a subexpression `E`, the expression `views::common(E)` is expression-equivalent to:

- `views::all(E)`, if `decltype((E))` models `common_range` and `views::all(E)` is a well-formed expression.
- Otherwise, `common_view{E}`.

**4** [*Example 1*:

```cpp
// Legacy algorithm:
template<class ForwardIterator>
size_t count(ForwardIterator first, ForwardIterator last);

template<forward_range R>
void my_algo(R&& r) {
  auto&& common = common_view{r};
  auto cnt = count(common.begin(), common.end());
  // ...
}
```

— *end example*]
