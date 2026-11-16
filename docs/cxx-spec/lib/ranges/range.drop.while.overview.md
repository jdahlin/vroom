# 24.7.9.1 Overview [range.drop.while.overview]

[Contents](../../README.md) · up: [24.7.9 Drop while view \[range.drop.while\]](range.drop.while.md) · prev: [24.7.9 Drop while view \[range.drop.while\]](range.drop.while.md) · next: [24.7.9.2 Class template `drop_while_view` \[range.drop.while.view\]](range.drop.while.view.md)

**1** Given a unary predicate `pred` and a `view` `r`, `drop_while_view` produces a `view` of the range `[ranges::find_if_not(r, pred), ranges::end(r))`.

**2** The name `views::drop_while` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given subexpressions `E` and `F`, the expression `views::drop_while(E, F)` is expression-equivalent to `drop_while_view{E, F}`.

**3** [*Example 1*:

```cpp
constexpr auto source = "  \t   \t   \t   hello there";
auto is_invisible = [](const auto x) { return x == ' ' || x == '\t'; };
auto skip_ws = drop_while_view{source, is_invisible};
for (auto c : skip_ws) {
  cout << c;                                    // prints hello there with no leading space
}
```

— *end example*]
