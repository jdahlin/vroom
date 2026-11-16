# 21.4.1 Header `<string_view>` synopsis [string.view.synop]

[Contents](../README.md) · up: [21.4 String view classes \[string.view\]](string.view.md) · prev: [21.4 String view classes \[string.view\]](string.view.md) · next: [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md)

```cpp
#include <compare>              // see [compare.syn]

namespace std {
  // [string.view.template], class template basic_string_view
  template<class charT, class traits = char_traits<charT>>
  class basic_string_view;

  template<class charT, class traits>
    inline constexpr bool ranges::enable_view<basic_string_view<charT, traits>> = true;
  template<class charT, class traits>
    inline constexpr bool ranges::enable_borrowed_range<basic_string_view<charT, traits>> = true;

  // [string.view.comparison], non-member comparison functions
  template<class charT, class traits>
    constexpr bool operator==(basic_string_view<charT, traits> x,
                              basic_string_view<charT, traits> y) noexcept;
  template<class charT, class traits>
    constexpr see below operator<=>(basic_string_view<charT, traits> x,
                                    basic_string_view<charT, traits> y) noexcept;

  // see [string.view.comparison], sufficient additional overloads of comparison functions

  // [string.view.io], inserters and extractors
  template<class charT, class traits>
    basic_ostream<charT, traits>&
      operator<<(basic_ostream<charT, traits>& os,
                 basic_string_view<charT, traits> str);

  // basic_string_view typedef names
  using string_view    = basic_string_view<char>;
  using u8string_view  = basic_string_view<char8_t>;
  using u16string_view = basic_string_view<char16_t>;
  using u32string_view = basic_string_view<char32_t>;
  using wstring_view   = basic_string_view<wchar_t>;

  // [string.view.hash], hash support
  template<class T> struct hash;
  template<> struct hash<string_view>;
  template<> struct hash<u8string_view>;
  template<> struct hash<u16string_view>;
  template<> struct hash<u32string_view>;
  template<> struct hash<wstring_view>;

  inline namespace literals {
  inline namespace string_view_literals {
    // [string.view.literals], suffix for basic_string_view literals
    constexpr string_view    operator""sv(const char* str, size_t len) noexcept;
    constexpr u8string_view  operator""sv(const char8_t* str, size_t len) noexcept;
    constexpr u16string_view operator""sv(const char16_t* str, size_t len) noexcept;
    constexpr u32string_view operator""sv(const char32_t* str, size_t len) noexcept;
    constexpr wstring_view   operator""sv(const wchar_t* str, size_t len) noexcept;
  }
  }
}
```

**1** The function templates defined in [[utility.swap]](utility.swap.md) and [[iterator.range]](iterator.range.md) are available when `<string_view>` is included.
