# 24.7.8.1 Overview [range.drop.overview]

[Contents](../../README.md) · up: [24.7.8 Drop view \[range.drop\]](range.drop.md) · prev: [24.7.8 Drop view \[range.drop\]](range.drop.md) · next: [24.7.8.2 Class template `drop_view` \[range.drop.view\]](range.drop.view.md)

**1** `drop_view` produces a `view` excluding the first *N* elements from another `view`, or an empty range if the adapted `view` contains fewer than *N* elements.

**2** The name `views::drop` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Let `E` and `F` be expressions, let `T` be `remove_cvref_t<decltype((E))>`, and let `D` be `range_difference_t<decltype((E))>`. If `decltype((F))` does not model `convertible_to<D>`, `views::drop(E, F)` is ill-formed. Otherwise, the expression `views::drop(E, F)` is expression-equivalent to:

- If `T` is a specialization of `ranges::empty_view` ([[range.empty.view]](range.empty.view.md)), then `((void) F, decay-copy(E))`.

- Otherwise, if `T` models `random_access_range` and `sized_range` and is

  - a specialization of `span` ([[views.span]](../containers/views.span.md)) where `T::extent == dynamic_extent`,
  - a specialization of `basic_string_view` ([[string.view]](../strings/string.view.md)),
  - a specialization of `ranges::iota_view` ([[range.iota.view]](range.iota.view.md)), or
  - a specialization of `ranges::subrange` ([[range.subrange]](range.subrange.md)),

  then `T{ranges::begin(E) + min<D>(ranges::size(E), F), ranges::end(E)}`, except that `E` is evaluated only once.

- Otherwise, `ranges::drop_view{E, F}`.

**3** [*Example 1*:

```cpp
auto ints = views::iota(0) | views::take(10);
auto latter_half = drop_view{ints, 5};
for (auto i : latter_half) {
  cout << i << ' ';                             // prints 5 6 7 8 9
}
```

— *end example*]
