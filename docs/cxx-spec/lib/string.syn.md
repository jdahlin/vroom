# 21.3.1 Header `<string>` synopsis [string.syn]

[Contents](../README.md) · up: [21.3 String classes \[string.classes\]](string.classes.md) · prev: [21.3 String classes \[string.classes\]](string.classes.md) · next: [21.3.2 Class template `basic_string` \[basic.string\]](basic.string.md)

```cpp
#include <compare>              // see [compare.syn]
#include <initializer_list>     // see [initializer.list.syn]

namespace std {
  // [char.traits], character traits
  template<class charT> struct char_traits;
  template<> struct char_traits<char>;
  template<> struct char_traits<char8_t>;
  template<> struct char_traits<char16_t>;
  template<> struct char_traits<char32_t>;
  template<> struct char_traits<wchar_t>;

  // [basic.string], basic_string
  template<class charT, class traits = char_traits<charT>, class Allocator = allocator<charT>>
    class basic_string;

  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(const basic_string<charT, traits, Allocator>& lhs,
                const basic_string<charT, traits, Allocator>& rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(basic_string<charT, traits, Allocator>&& lhs,
                const basic_string<charT, traits, Allocator>& rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(const basic_string<charT, traits, Allocator>& lhs,
                basic_string<charT, traits, Allocator>&& rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(basic_string<charT, traits, Allocator>&& lhs,
                basic_string<charT, traits, Allocator>&& rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(const charT* lhs,
                const basic_string<charT, traits, Allocator>& rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(const charT* lhs,
                basic_string<charT, traits, Allocator>&& rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(charT lhs,
                const basic_string<charT, traits, Allocator>& rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(charT lhs,
                basic_string<charT, traits, Allocator>&& rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(const basic_string<charT, traits, Allocator>& lhs,
                const charT* rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(basic_string<charT, traits, Allocator>&& lhs,
                const charT* rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(const basic_string<charT, traits, Allocator>& lhs,
                charT rhs);
  template<class charT, class traits, class Allocator>
    constexpr basic_string<charT, traits, Allocator>
      operator+(basic_string<charT, traits, Allocator>&& lhs,
                charT rhs);

  template<class charT, class traits, class Allocator>
    constexpr bool
      operator==(const basic_string<charT, traits, Allocator>& lhs,
                 const basic_string<charT, traits, Allocator>& rhs) noexcept;
  template<class charT, class traits, class Allocator>
    constexpr bool operator==(const basic_string<charT, traits, Allocator>& lhs,
                              const charT* rhs);

  template<class charT, class traits, class Allocator>
    constexpr see below operator<=>(const basic_string<charT, traits, Allocator>& lhs,
                                    const basic_string<charT, traits, Allocator>& rhs) noexcept;
  template<class charT, class traits, class Allocator>
    constexpr see below operator<=>(const basic_string<charT, traits, Allocator>& lhs,
                                    const charT* rhs);

  // [string.special], swap
  template<class charT, class traits, class Allocator>
    constexpr void
      swap(basic_string<charT, traits, Allocator>& lhs,
           basic_string<charT, traits, Allocator>& rhs)
        noexcept(noexcept(lhs.swap(rhs)));

  // [string.io], inserters and extractors
  template<class charT, class traits, class Allocator>
    basic_istream<charT, traits>&
      operator>>(basic_istream<charT, traits>& is,
                 basic_string<charT, traits, Allocator>& str);
  template<class charT, class traits, class Allocator>
    basic_ostream<charT, traits>&
      operator<<(basic_ostream<charT, traits>& os,
                 const basic_string<charT, traits, Allocator>& str);
  template<class charT, class traits, class Allocator>
    basic_istream<charT, traits>&
      getline(basic_istream<charT, traits>& is,
              basic_string<charT, traits, Allocator>& str,
              charT delim);
  template<class charT, class traits, class Allocator>
    basic_istream<charT, traits>&
      getline(basic_istream<charT, traits>&& is,
              basic_string<charT, traits, Allocator>& str,
              charT delim);
  template<class charT, class traits, class Allocator>
    basic_istream<charT, traits>&
      getline(basic_istream<charT, traits>& is,
              basic_string<charT, traits, Allocator>& str);
  template<class charT, class traits, class Allocator>
    basic_istream<charT, traits>&
      getline(basic_istream<charT, traits>&& is,
              basic_string<charT, traits, Allocator>& str);

  // [string.erasure], erasure
  template<class charT, class traits, class Allocator, class U>
    constexpr typename basic_string<charT, traits, Allocator>::size_type
      erase(basic_string<charT, traits, Allocator>& c, const U& value);
  template<class charT, class traits, class Allocator, class Predicate>
    constexpr typename basic_string<charT, traits, Allocator>::size_type
      erase_if(basic_string<charT, traits, Allocator>& c, Predicate pred);

  // basic_string typedef names
  using string    = basic_string<char>;
  using u8string  = basic_string<char8_t>;
  using u16string = basic_string<char16_t>;
  using u32string = basic_string<char32_t>;
  using wstring   = basic_string<wchar_t>;

  // [string.conversions], numeric conversions
  int stoi(const string& str, size_t* idx = nullptr, int base = 10);
  long stol(const string& str, size_t* idx = nullptr, int base = 10);
  unsigned long stoul(const string& str, size_t* idx = nullptr, int base = 10);
  long long stoll(const string& str, size_t* idx = nullptr, int base = 10);
  unsigned long long stoull(const string& str, size_t* idx = nullptr, int base = 10);
  float stof(const string& str, size_t* idx = nullptr);
  double stod(const string& str, size_t* idx = nullptr);
  long double stold(const string& str, size_t* idx = nullptr);
  string to_string(int val);
  string to_string(unsigned val);
  string to_string(long val);
  string to_string(unsigned long val);
  string to_string(long long val);
  string to_string(unsigned long long val);
  string to_string(float val);
  string to_string(double val);
  string to_string(long double val);

  int stoi(const wstring& str, size_t* idx = nullptr, int base = 10);
  long stol(const wstring& str, size_t* idx = nullptr, int base = 10);
  unsigned long stoul(const wstring& str, size_t* idx = nullptr, int base = 10);
  long long stoll(const wstring& str, size_t* idx = nullptr, int base = 10);
  unsigned long long stoull(const wstring& str, size_t* idx = nullptr, int base = 10);
  float stof(const wstring& str, size_t* idx = nullptr);
  double stod(const wstring& str, size_t* idx = nullptr);
  long double stold(const wstring& str, size_t* idx = nullptr);
  wstring to_wstring(int val);
  wstring to_wstring(unsigned val);
  wstring to_wstring(long val);
  wstring to_wstring(unsigned long val);
  wstring to_wstring(long long val);
  wstring to_wstring(unsigned long long val);
  wstring to_wstring(float val);
  wstring to_wstring(double val);
  wstring to_wstring(long double val);

  namespace pmr {
    template<class charT, class traits = char_traits<charT>>
      using basic_string = std::basic_string<charT, traits, polymorphic_allocator<charT>>;

    using string    = basic_string<char>;
    using u8string  = basic_string<char8_t>;
    using u16string = basic_string<char16_t>;
    using u32string = basic_string<char32_t>;
    using wstring   = basic_string<wchar_t>;
  }

  // [basic.string.hash], hash support
  template<class T> struct hash;
  template<> struct hash<string>;
  template<> struct hash<u8string>;
  template<> struct hash<u16string>;
  template<> struct hash<u32string>;
  template<> struct hash<wstring>;
  template<> struct hash<pmr::string>;
  template<> struct hash<pmr::u8string>;
  template<> struct hash<pmr::u16string>;
  template<> struct hash<pmr::u32string>;
  template<> struct hash<pmr::wstring>;

  inline namespace literals {
  inline namespace string_literals {
    // [basic.string.literals], suffix for basic_string literals
    constexpr string    operator""s(const char* str, size_t len);
    constexpr u8string  operator""s(const char8_t* str, size_t len);
    constexpr u16string operator""s(const char16_t* str, size_t len);
    constexpr u32string operator""s(const char32_t* str, size_t len);
    constexpr wstring   operator""s(const wchar_t* str, size_t len);
  }
  }
}
```
