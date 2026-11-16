# 24.7.10.1 Overview [range.join.overview]

[Contents](../README.md) · up: [24.7.10 Join view \[range.join\]](range.join.md) · prev: [24.7.10 Join view \[range.join\]](range.join.md) · next: [24.7.10.2 Class template `join_view` \[range.join.view\]](range.join.view.md)

**1** `join_view` flattens a `view` of ranges into a `view`.

**2** The name `views::join` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given a subexpression `E`, the expression `views::join(E)` is expression-equivalent to `join_view{E}`.

**3** [*Example 1*:

```cpp
vector<string> ss{"hello", " ", "world", "!"};
join_view greeting{ss};
for (char ch : greeting)
  cout << ch;                                   // prints: hello world!
```

— *end example*]
