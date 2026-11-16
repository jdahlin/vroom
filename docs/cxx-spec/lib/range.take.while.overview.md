# 24.7.7.1 Overview [range.take.while.overview]

[Contents](../README.md) · up: [24.7.7 Take while view \[range.take.while\]](range.take.while.md) · prev: [24.7.7 Take while view \[range.take.while\]](range.take.while.md) · next: [24.7.7.2 Class template `take_while_view` \[range.take.while.view\]](range.take.while.view.md)

**1** Given a unary predicate `pred` and a `view` `r`, `take_while_view` produces a `view` of the range `[begin(r), ranges::find_if_not(r, pred))`.

**2** The name `views::take_while` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given subexpressions `E` and `F`, the expression `views::take_while(E, F)` is expression-equivalent to `take_while_view{E, F}`.

**3** [*Example 1*:

```cpp
auto input = istringstream{"0 1 2 3 4 5 6 7 8 9"};
auto small = [](const auto x) noexcept { return x < 5; };
auto small_ints = istream_view<int>(input) | views::take_while(small);
for (const auto i : small_ints) {
  cout << i << ' ';                             // prints 0 1 2 3 4
}
auto i = 0;
input >> i;
cout << i;                                      // prints 6
```

— *end example*]
