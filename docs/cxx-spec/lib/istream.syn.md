# 29.7.1 Header `<istream>` synopsis [istream.syn]

[Contents](../README.md) · up: [29.7 Formatting and manipulators \[iostream.format\]](iostream.format.md) · prev: [29.7 Formatting and manipulators \[iostream.format\]](iostream.format.md) · next: [29.7.2 Header `<ostream>` synopsis \[ostream.syn\]](ostream.syn.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
    class basic_istream;

  using istream  = basic_istream<char>;
  using wistream = basic_istream<wchar_t>;

  template<class charT, class traits = char_traits<charT>>
    class basic_iostream;

  using iostream  = basic_iostream<char>;
  using wiostream = basic_iostream<wchar_t>;

  template<class charT, class traits>
    basic_istream<charT, traits>& ws(basic_istream<charT, traits>& is);

  template<class charT, class traits, class T>
    basic_istream<charT, traits>& operator>>(basic_istream<charT, traits>&& is, T&& x);
}
```
