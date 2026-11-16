# 21.4.2.1 Construction and assignment [string.view.cons]

[Contents](../README.md) · up: [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md) · prev: [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md) · next: [21.4.2.2 Iterator support \[string.view.iterators\]](string.view.iterators.md)

```cpp
constexpr basic_string_view() noexcept;
```

**1** *Postconditions:* `size_ == 0` and `data_ == nullptr`.

```cpp
constexpr basic_string_view(const charT* str);
```

**2** *Preconditions:* `[str, str + traits::length(str))` is a valid range.

**3** *Effects:* Constructs a `basic_string_view`, initializing `data_` with `str` and initializing `size_` with `traits::length(str)`.

**4** *Complexity:* 𝑂(`traits::length(str)`).

```cpp
constexpr basic_string_view(const charT* str, size_type len);
```

**5** *Preconditions:* `[str, str + len)` is a valid range.

**6** *Effects:* Constructs a `basic_string_view`, initializing `data_` with `str` and initializing `size_` with `len`.

```cpp
template<class It, class End>
  constexpr basic_string_view(It begin, End end);
```

**7** *Constraints:*

- `It` satisfies `contiguous_iterator`.
- `End` satisfies `sized_sentinel_for<It>`.
- `is_same_v<iter_value_t<It>, charT>` is `true`.
- `is_convertible_v<End, size_type>` is `false`.

**8** *Preconditions:*

- `[begin, end)` is a valid range.
- `It` models `contiguous_iterator`.
- `End` models `sized_sentinel_for<It>`.

**9** *Effects:* Initializes `data_` with `to_address(begin)` and initializes `size_` with `end - begin`.
