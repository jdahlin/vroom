# 22.7.3.2 Constructors, copy, and assignment [span.cons]

[Contents](../../README.md) · up: [22.7.3 Class template `span` \[views.span\]](views.span.md) · prev: [22.7.3.1 Overview \[span.overview\]](span.overview.md) · next: [22.7.3.3 Deduction guides \[span.deduct\]](span.deduct.md)

```cpp
constexpr span() noexcept;
```

**1** *Constraints:* `Extent == dynamic_extent || Extent == 0` is `true`.

**2** *Postconditions:* `size() == 0 && data() == nullptr`.

```cpp
template<class It>
  constexpr explicit(extent != dynamic_extent) span(It first, size_type count);
```

**3** *Constraints:* Let `U` be `remove_reference_t<iter_reference_t<It>>`.

- `It` satisfies `contiguous_iterator`.
- `is_convertible_v<U(*)[], element_type(*)[]>` is `true`. [*Note 1*: The intent is to allow only qualification conversions of the iterator reference type to `element_type`. — *end note*]

**4** *Preconditions:*

- `[first, first + count)` is a valid range.
- `It` models `contiguous_iterator`.
- If `extent` is not equal to `dynamic_extent`, then `count` is equal to `extent`.

**5** *Effects:* Initializes `data_` with `to_address(first)` and `size_` with `count`.

**6** *Throws:* Nothing.

```cpp
template<class It, class End>
  constexpr explicit(extent != dynamic_extent) span(It first, End last);
```

**7** *Constraints:* Let `U` be `remove_reference_t<iter_reference_t<It>>`.

- `is_convertible_v<U(*)[], element_type(*)[]>` is `true`. [*Note 2*: The intent is to allow only qualification conversions of the iterator reference type to `element_type`. — *end note*]
- `It` satisfies `contiguous_iterator`.
- `End` satisfies `sized_sentinel_for<It>`.
- `is_convertible_v<End, size_t>` is `false`.

**8** *Preconditions:*

- If `extent` is not equal to `dynamic_extent`, then `last - first` is equal to `extent`.
- `[first, last)` is a valid range.
- `It` models `contiguous_iterator`.
- `End` models `sized_sentinel_for<It>`.

**9** *Effects:* Initializes `data_` with `to_address(first)` and `size_` with `last - first`.

**10** *Throws:* When and what `last - first` throws.

```cpp
template<size_t N> constexpr span(type_identity_t<element_type> (&arr)[N]) noexcept;
template<class T, size_t N> constexpr span(array<T, N>& arr) noexcept;
template<class T, size_t N> constexpr span(const array<T, N>& arr) noexcept;
```

**11** *Constraints:* Let `U` be `remove_pointer_t<decltype(data(arr))>`.

- `extent == dynamic_extent || N == extent` is `true`, and
- `is_convertible_v<U(*)[], element_type(*)[]>` is `true`. [*Note 3*: The intent is to allow only qualification conversions of the array element type to `element_type`. — *end note*]

**12** *Effects:* Constructs a `span` that is a view over the supplied array. [*Note 4*: `type_identity_t` affects class template argument deduction. — *end note*]

**13** *Postconditions:* `size() == N && data() == data(arr)` is `true`.

```cpp
template<class R> constexpr explicit(extent != dynamic_extent) span(R&& r);
```

**14** *Constraints:* Let `U` be `remove_reference_t<ranges::range_reference_t<R>>`.

- `R` satisfies `ranges::contiguous_range` and `ranges::sized_range`.
- Either `R` satisfies `ranges::borrowed_range` or `is_const_v<element_type>` is `true`.
- `remove_cvref_t<R>` is not a specialization of `span`.
- `remove_cvref_t<R>` is not a specialization of `array`.
- `is_array_v<remove_cvref_t<R>>` is `false`.
- `is_convertible_v<U(*)[], element_type(*)[]>` is `true`. [*Note 5*: The intent is to allow only qualification conversions of the range reference type to `element_type`. — *end note*]

**15** *Preconditions:*

- If `extent` is not equal to `dynamic_extent`, then `ranges::size(r)` is equal to `extent`.
- `R` models `ranges::contiguous_range` and `ranges::sized_range`.
- If `is_const_v<element_type>` is `false`, `R` models `ranges::borrowed_range`.

**16** *Effects:* Initializes `data_` with `ranges::data(r)` and `size_` with `ranges::size(r)`.

**17** *Throws:* What and when `ranges::data(r)` and `ranges::size(r)` throw.

```cpp
constexpr span(const span& other) noexcept = default;
```

**18** *Postconditions:* `other.size() == size() && other.data() == data()`.

```cpp
template<class OtherElementType, size_t OtherExtent>
  constexpr explicit(see below) span(const span<OtherElementType, OtherExtent>& s) noexcept;
```

**19** *Constraints:*

- `extent == dynamic_extent` `||` `OtherExtent == dynamic_extent` `||` `extent == OtherExtent` is `true`, and
- `is_convertible_v<OtherElementType(*)[], element_type(*)[]>` is `true`. [*Note 6*: The intent is to allow only qualification conversions of the `OtherElementType` to `element_type`. — *end note*]

**20** *Preconditions:* If `extent` is not equal to `dynamic_extent`, then `s.size()` is equal to `extent`.

**21** *Effects:* Constructs a `span` that is a view over the range `[s.data(), s.data() + s.size())`.

**22** *Postconditions:* `size() == s.size() && data() == s.data()`.

**23** *Remarks:* The expression inside `explicit` is equivalent to:

```cpp
extent != dynamic_extent && OtherExtent == dynamic_extent
```

```cpp
constexpr span& operator=(const span& other) noexcept = default;
```

**24** *Postconditions:* `size() == other.size() && data() == other.data()`.
