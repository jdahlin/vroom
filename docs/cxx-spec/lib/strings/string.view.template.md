# 21.4.2 Class template `basic_string_view` [string.view.template]

[Contents](../../README.md) · up: [21.4 String view classes \[string.view\]](string.view.md) · prev: [21.4.1 Header `<string_view>` synopsis \[string.view.synop\]](string.view.synop.md) · next: [21.4.2.1 Construction and assignment \[string.view.cons\]](string.view.cons.md)

```cpp
template<class charT, class traits = char_traits<charT>>
class basic_string_view {
public:
  // types
  using traits_type            = traits;
  using value_type             = charT;
  using pointer                = value_type*;
  using const_pointer          = const value_type*;
  using reference              = value_type&;
  using const_reference        = const value_type&;
  using const_iterator         = implementation-defined; // see [string.view.iterators]
  using iterator               = const_iterator;
  using const_reverse_iterator = reverse_iterator<const_iterator>;
  using reverse_iterator       = const_reverse_iterator;
  using size_type              = size_t;
  using difference_type        = ptrdiff_t;
  static constexpr size_type npos = size_type(-1);

  // [string.view.cons], construction and assignment
  constexpr basic_string_view() noexcept;
  constexpr basic_string_view(const basic_string_view&) noexcept = default;
  constexpr basic_string_view& operator=(const basic_string_view&) noexcept = default;
  constexpr basic_string_view(const charT* str);
  constexpr basic_string_view(const charT* str, size_type len);
  template<class It, class End>
    constexpr basic_string_view(It begin, End end);

  // [string.view.iterators], iterator support
  constexpr const_iterator begin() const noexcept;
  constexpr const_iterator end() const noexcept;
  constexpr const_iterator cbegin() const noexcept;
  constexpr const_iterator cend() const noexcept;
  constexpr const_reverse_iterator rbegin() const noexcept;
  constexpr const_reverse_iterator rend() const noexcept;
  constexpr const_reverse_iterator crbegin() const noexcept;
  constexpr const_reverse_iterator crend() const noexcept;

  // [string.view.capacity], capacity
  constexpr size_type size() const noexcept;
  constexpr size_type length() const noexcept;
  constexpr size_type max_size() const noexcept;
  [[nodiscard]] constexpr bool empty() const noexcept;

  // [string.view.access], element access
  constexpr const_reference operator[](size_type pos) const;
  constexpr const_reference at(size_type pos) const;
  constexpr const_reference front() const;
  constexpr const_reference back() const;
  constexpr const_pointer data() const noexcept;

  // [string.view.modifiers], modifiers
  constexpr void remove_prefix(size_type n);
  constexpr void remove_suffix(size_type n);
  constexpr void swap(basic_string_view& s) noexcept;

  // [string.view.ops], string operations
  constexpr size_type copy(charT* s, size_type n, size_type pos = 0) const;

  constexpr basic_string_view substr(size_type pos = 0, size_type n = npos) const;

  constexpr int compare(basic_string_view s) const noexcept;
  constexpr int compare(size_type pos1, size_type n1, basic_string_view s) const;
  constexpr int compare(size_type pos1, size_type n1, basic_string_view s,
                        size_type pos2, size_type n2) const;
  constexpr int compare(const charT* s) const;
  constexpr int compare(size_type pos1, size_type n1, const charT* s) const;
  constexpr int compare(size_type pos1, size_type n1, const charT* s, size_type n2) const;

  constexpr bool starts_with(basic_string_view x) const noexcept;
  constexpr bool starts_with(charT x) const noexcept;
  constexpr bool starts_with(const charT* x) const;
  constexpr bool ends_with(basic_string_view x) const noexcept;
  constexpr bool ends_with(charT x) const noexcept;
  constexpr bool ends_with(const charT* x) const;

  // [string.view.find], searching
  constexpr size_type find(basic_string_view s, size_type pos = 0) const noexcept;
  constexpr size_type find(charT c, size_type pos = 0) const noexcept;
  constexpr size_type find(const charT* s, size_type pos, size_type n) const;
  constexpr size_type find(const charT* s, size_type pos = 0) const;
  constexpr size_type rfind(basic_string_view s, size_type pos = npos) const noexcept;
  constexpr size_type rfind(charT c, size_type pos = npos) const noexcept;
  constexpr size_type rfind(const charT* s, size_type pos, size_type n) const;
  constexpr size_type rfind(const charT* s, size_type pos = npos) const;

  constexpr size_type find_first_of(basic_string_view s, size_type pos = 0) const noexcept;
  constexpr size_type find_first_of(charT c, size_type pos = 0) const noexcept;
  constexpr size_type find_first_of(const charT* s, size_type pos, size_type n) const;
  constexpr size_type find_first_of(const charT* s, size_type pos = 0) const;
  constexpr size_type find_last_of(basic_string_view s, size_type pos = npos) const noexcept;
  constexpr size_type find_last_of(charT c, size_type pos = npos) const noexcept;
  constexpr size_type find_last_of(const charT* s, size_type pos, size_type n) const;
  constexpr size_type find_last_of(const charT* s, size_type pos = npos) const;
  constexpr size_type find_first_not_of(basic_string_view s, size_type pos = 0) const noexcept;
  constexpr size_type find_first_not_of(charT c, size_type pos = 0) const noexcept;
  constexpr size_type find_first_not_of(const charT* s, size_type pos,
                                        size_type n) const;
  constexpr size_type find_first_not_of(const charT* s, size_type pos = 0) const;
  constexpr size_type find_last_not_of(basic_string_view s,
                                       size_type pos = npos) const noexcept;
  constexpr size_type find_last_not_of(charT c, size_type pos = npos) const noexcept;
  constexpr size_type find_last_not_of(const charT* s, size_type pos,
                                       size_type n) const;
  constexpr size_type find_last_not_of(const charT* s, size_type pos = npos) const;

private:
  const_pointer data_;          // exposition only
  size_type size_;              // exposition only
};

// [string.view.deduct], deduction guide
template<class It, class End>
  basic_string_view(It, End) -> basic_string_view<iter_value_t<It>>;
```

**1** In every specialization `basic_string_view<charT, traits>`, the type `traits` shall meet the character traits requirements ([[char.traits]](char.traits.md)). [*Note 1*: The program is ill-formed if `traits::char_type` is not the same type as `charT`. — *end note*]

**2** For a `basic_string_view str`, any operation that invalidates a pointer in the range `[str.data(), str.data() + str.size())` invalidates pointers, iterators, and references returned from `str`’s member functions.

**3** The complexity of `basic_string_view` member functions is 𝑂(1) unless otherwise specified.

## Subclauses

- [21.4.2.1 Construction and assignment \[string.view.cons\]](string.view.cons.md)
- [21.4.2.2 Iterator support \[string.view.iterators\]](string.view.iterators.md)
- [21.4.2.3 Capacity \[string.view.capacity\]](string.view.capacity.md)
- [21.4.2.4 Element access \[string.view.access\]](string.view.access.md)
- [21.4.2.5 Modifiers \[string.view.modifiers\]](string.view.modifiers.md)
- [21.4.2.6 String operations \[string.view.ops\]](string.view.ops.md)
- [21.4.2.7 Searching \[string.view.find\]](string.view.find.md)
