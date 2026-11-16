# 24.7.14.1 Overview [range.reverse.overview]

[Contents](../../README.md) · up: [24.7.14 Reverse view \[range.reverse\]](range.reverse.md) · prev: [24.7.14 Reverse view \[range.reverse\]](range.reverse.md) · next: [24.7.14.2 Class template `reverse_view` \[range.reverse.view\]](range.reverse.view.md)

**1** `reverse_view` takes a bidirectional `view` and produces another `view` that iterates the same elements in reverse order.

**2** The name `views::reverse` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given a subexpression `E`, the expression `views::reverse(E)` is expression-equivalent to:

- If the type of `E` is a (possibly cv-qualified) specialization of `reverse_view`, equivalent to `E.base()`.

- Otherwise, if the type of `E` is cv-qualified

  ```cpp
  subrange<reverse_iterator<I>, reverse_iterator<I>, K>
  ```

  for some iterator type `I` and value `K` of type `subrange_kind`,

  - if `K` is `subrange_kind::sized`, equivalent to:

    ```cpp
    subrange<I, I, K>(E.end().base(), E.begin().base(), E.size())
    ```

  - otherwise, equivalent to:

    ```cpp
    subrange<I, I, K>(E.end().base(), E.begin().base())
    ```

  However, in either case `E` is evaluated only once.

- Otherwise, equivalent to `reverse_view{E}`.

**3** [*Example 1*:

```cpp
vector<int> is {0,1,2,3,4};
reverse_view rv {is};
for (int i : rv)
  cout << i << ' '; // prints: 4 3 2 1 0
```

— *end example*]
