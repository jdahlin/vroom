# 24.7.6.1 Overview [range.take.overview]

[Contents](../README.md) · up: [24.7.6 Take view \[range.take\]](range.take.md) · prev: [24.7.6 Take view \[range.take\]](range.take.md) · next: [24.7.6.2 Class template `take_view` \[range.take.view\]](range.take.view.md)

**1** `take_view` produces a `view` of the first *N* elements from another `view`, or all the elements if the adapted `view` contains fewer than *N*.

**2** The name `views::take` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Let `E` and `F` be expressions, let `T` be `remove_cvref_t<decltype((E))>`, and let `D` be `range_difference_t<decltype((E))>`. If `decltype((F))` does not model `convertible_to<D>`, `views::take(E, F)` is ill-formed. Otherwise, the expression `views::take(E, F)` is expression-equivalent to:

- If `T` is a specialization of `ranges::empty_view` ([[range.empty.view]](range.empty.view.md)), then `((void) F, decay-copy(E))`.

- Otherwise, if `T` models `random_access_range` and `sized_range` and is

  - a specialization of `span` ([[views.span]](views.span.md)) where `T::extent == dynamic_extent`,
  - a specialization of `basic_string_view` ([[string.view]](string.view.md)),
  - a specialization of `ranges::iota_view` ([[range.iota.view]](range.iota.view.md)), or
  - a specialization of `ranges::subrange` ([[range.subrange]](range.subrange.md)),

  then `T{ranges::begin(E), ranges::begin(E) + min<D>(ranges::size(E), F)}`, except that `E` is evaluated only once.

- Otherwise, `ranges::take_view{E, F}`.

**3** [*Example 1*:

```cpp
vector<int> is{0,1,2,3,4,5,6,7,8,9};
take_view few{is, 5};
for (int i : few)
  cout << i << ' '; // prints: 0 1 2 3 4
```

— *end example*]
