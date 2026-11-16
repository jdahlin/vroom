# 24.6.2.1 Overview [range.single.overview]

[Contents](../README.md) · up: [24.6.2 Single view \[range.single\]](range.single.md) · prev: [24.6.2 Single view \[range.single\]](range.single.md) · next: [24.6.2.2 Class template `single_view` \[range.single.view\]](range.single.view.md)

**1** `single_view` produces a `view` that contains exactly one element of a specified value.

**2** The name `views::single` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). Given a subexpression `E`, the expression `views::single(E)` is expression-equivalent to `single_view{E}`.

**3** [*Example 1*:

```cpp
single_view s{4};
for (int i : s)
  cout << i;        // prints 4
```

— *end example*]
