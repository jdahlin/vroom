# 24.6.3.1 Overview [range.iota.overview]

[Contents](../README.md) · up: [24.6.3 Iota view \[range.iota\]](range.iota.md) · prev: [24.6.3 Iota view \[range.iota\]](range.iota.md) · next: [24.6.3.2 Class template `iota_view` \[range.iota.view\]](range.iota.view.md)

**1** `iota_view` generates a sequence of elements by repeatedly incrementing an initial value.

**2** The name `views::iota` denotes a customization point object ([[customization.point.object]](customization.point.object.md)). Given subexpressions `E` and `F`, the expressions `views::iota(E)` and `views::iota(E, F)` are expression-equivalent to `iota_view{E}` and `iota_view{E, F}`, respectively.

**3** [*Example 1*:

```cpp
for (int i : iota_view{1, 10})
  cout << i << ' '; // prints: 1 2 3 4 5 6 7 8 9
```

— *end example*]
