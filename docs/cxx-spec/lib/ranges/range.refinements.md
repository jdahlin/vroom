# 24.4.5 Other range refinements [range.refinements]

[Contents](../../README.md) · up: [24.4 Range requirements \[range.req\]](range.req.md) · prev: [24.4.4 Views \[range.view\]](range.view.md) · next: [24.5 Range utilities \[range.utility\]](range.utility.md)

**1** The `output_range` concept specifies requirements of a `range` type for which `ranges::begin` returns a model of `output_iterator` ([[iterator.concept.output]](../iterators/iterator.concept.output.md)). `input_range`, `forward_range`, `bidirectional_range`, and `random_access_range` are defined similarly.

```cpp
template<class R, class T>
  concept output_range =
    range<R> && output_iterator<iterator_t<R>, T>;

template<class T>
  concept input_range =
    range<T> && input_iterator<iterator_t<T>>;

template<class T>
  concept forward_range =
    input_range<T> && forward_iterator<iterator_t<T>>;

template<class T>
  concept bidirectional_range =
    forward_range<T> && bidirectional_iterator<iterator_t<T>>;

template<class T>
  concept random_access_range =
    bidirectional_range<T> && random_access_iterator<iterator_t<T>>;
```

**2** `contiguous_range` additionally requires that the `ranges::data` customization point ([[range.prim.data]](range.prim.data.md)) is usable with the range.

```cpp
template<class T>
  concept contiguous_range =
    random_access_range<T> && contiguous_iterator<iterator_t<T>> &&
    requires(T& t) {
      { ranges::data(t) } -> same_as<add_pointer_t<range_reference_t<T>>>;
    };
```

**3** Given an expression `t` such that `decltype((t))` is `T&`, `T` models `contiguous_range` only if `to_address(ranges::begin(t)) == ranges::data(t)` is `true`.

**4** The `common_range` concept specifies requirements of a `range` type for which `ranges::begin` and `ranges::end` return objects of the same type. [*Example 1*: The standard containers ([[containers]](../containers/containers.md)) model `common_range`. — *end example*]

```cpp
template<class T>
  concept common_range =
    range<T> && same_as<iterator_t<T>, sentinel_t<T>>;
```

**5** The `viewable_range` concept specifies the requirements of a `range` type that can be converted to a `view` safely.

```cpp
template<class T>
  concept viewable_range =
    range<T> && (borrowed_range<T> || view<remove_cvref_t<T>>);
```
