# 21.4.3 Deduction guide [string.view.deduct]

[Contents](../README.md) · up: [21.4 String view classes \[string.view\]](string.view.md) · prev: [21.4.2.7 Searching \[string.view.find\]](string.view.find.md) · next: [21.4.4 Non-member comparison functions \[string.view.comparison\]](string.view.comparison.md)

```cpp
template<class It, class End>
  basic_string_view(It, End) -> basic_string_view<iter_value_t<It>>;
```

**1** *Constraints:*

- `It` satisfies `contiguous_iterator`.
- `End` satisfies `sized_sentinel_for<It>`.
