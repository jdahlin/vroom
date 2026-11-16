# 24.7.11.1 Overview [range.split.overview]

[Contents](../README.md) · up: [24.7.11 Split view \[range.split\]](range.split.md) · prev: [24.7.11 Split view \[range.split\]](range.split.md) · next: [24.7.11.2 Class template `split_view` \[range.split.view\]](range.split.view.md)

**1** `split_view` takes a `view` and a delimiter, and splits the `view` into subranges on the delimiter. The delimiter can be a single element or a `view` of elements.

**2** The name `views::split` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given subexpressions `E` and `F`, the expression `views::split(E, F)` is expression-equivalent to `split_view{E, F}`.

**3** [*Example 1*:

```cpp
string str{"the quick brown fox"};
split_view sentence{str, ' '};
for (auto word : sentence) {
  for (char ch : word)
    cout << ch;
  cout << '*';
}
// The above prints: the*quick*brown*fox*
```

— *end example*]
