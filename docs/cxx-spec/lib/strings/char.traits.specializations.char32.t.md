# 21.2.3.4 `struct char_traits<char32_t>` [char.traits.specializations.char32.t]

[Contents](../../README.md) · up: [21.2.3 `char_traits` specializations \[char.traits.specializations\]](char.traits.specializations.md) · prev: [21.2.3.3 `struct char_traits<char16_t>` \[char.traits.specializations.char16.t\]](char.traits.specializations.char16.t.md) · next: [21.2.3.5 `struct char_traits<wchar_t>` \[char.traits.specializations.wchar.t\]](char.traits.specializations.wchar.t.md)

```cpp
namespace std {
  template<> struct char_traits<char32_t> {
    using char_type  = char32_t;
    using int_type   = uint_least32_t;
    using off_type   = streamoff;
    using pos_type   = u32streampos;
    using state_type = mbstate_t;
    using comparison_category = strong_ordering;

    static constexpr void assign(char_type& c1, const char_type& c2) noexcept;
    static constexpr bool eq(char_type c1, char_type c2) noexcept;
    static constexpr bool lt(char_type c1, char_type c2) noexcept;

    static constexpr int compare(const char_type* s1, const char_type* s2, size_t n);
    static constexpr size_t length(const char_type* s);
    static constexpr const char_type* find(const char_type* s, size_t n,
                                           const char_type& a);
    static constexpr char_type* move(char_type* s1, const char_type* s2, size_t n);
    static constexpr char_type* copy(char_type* s1, const char_type* s2, size_t n);
    static constexpr char_type* assign(char_type* s, size_t n, char_type a);

    static constexpr int_type not_eof(int_type c) noexcept;
    static constexpr char_type to_char_type(int_type c) noexcept;
    static constexpr int_type to_int_type(char_type c) noexcept;
    static constexpr bool eq_int_type(int_type c1, int_type c2) noexcept;
    static constexpr int_type eof() noexcept;
  };
}
```

**1** The two-argument members `assign`, `eq`, and `lt` are defined identically to the built-in operators `=`, `==`, and `<`, respectively.

**2** The member `eof()` returns an implementation-defined constant that cannot appear as a Unicode code point.
