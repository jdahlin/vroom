# 24.7.15.1 Overview [range.elements.overview]

[Contents](../../README.md) · up: [24.7.15 Elements view \[range.elements\]](range.elements.md) · prev: [24.7.15 Elements view \[range.elements\]](range.elements.md) · next: [24.7.15.2 Class template `elements_view` \[range.elements.view\]](range.elements.view.md)

**1** `elements_view` takes a `view` of tuple-like values and a `size_t`, and produces a `view` with a value-type of the *N*<sup></sup>*th* element of the adapted `view`’s value-type.

**2** The name `views::elements<N>` denotes a range adaptor object ([[range.adaptor.object]](range.adaptor.object.md)). Given a subexpression `E` and constant expression `N`, the expression `views::elements<N>(E)` is expression-equivalent to `elements_view<views::all_t<decltype((E))>, N>{E}`.

[*Example 1*:

```cpp
auto historical_figures = map{
  {"Lovelace"sv, 1815},
  {"Turing"sv, 1912},
  {"Babbage"sv, 1791},
  {"Hamilton"sv, 1936}
};

auto names = historical_figures | views::elements<0>;
for (auto&& name : names) {
  cout << name << ' ';          // prints Babbage Hamilton Lovelace Turing
}

auto birth_years = historical_figures | views::elements<1>;
for (auto&& born : birth_years) {
  cout << born << ' ';          // prints 1791 1936 1815 1912
}
```

— *end example*]

**3** `keys_view` is an alias for `elements_view<views::all_t<R>, 0>`, and is useful for extracting keys from associative containers.

[*Example 2*:

```cpp
auto names = keys_view{historical_figures};
for (auto&& name : names) {
  cout << name << ' ';          // prints Babbage Hamilton Lovelace Turing
}
```

— *end example*]

**4** `values_view` is an alias for `elements_view<views::all_t<R>, 1>`, and is useful for extracting values from associative containers.

[*Example 3*:

```cpp
auto is_even = [](const auto x) { return x % 2 == 0; };
cout << ranges::count_if(values_view{historical_figures}, is_even);     // prints 2
```

— *end example*]
