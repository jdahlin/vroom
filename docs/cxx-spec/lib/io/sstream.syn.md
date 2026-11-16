# 29.8.1 Header `<sstream>` synopsis [sstream.syn]

[Contents](../../README.md) · up: [29.8 String-based streams \[string.streams\]](string.streams.md) · prev: [29.8 String-based streams \[string.streams\]](string.streams.md) · next: [29.8.2 Class template `basic_stringbuf` \[stringbuf\]](stringbuf.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
    class basic_stringbuf;

  using stringbuf  = basic_stringbuf<char>;
  using wstringbuf = basic_stringbuf<wchar_t>;

  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
    class basic_istringstream;

  using istringstream  = basic_istringstream<char>;
  using wistringstream = basic_istringstream<wchar_t>;

  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
    class basic_ostringstream;
  using ostringstream  = basic_ostringstream<char>;
  using wostringstream = basic_ostringstream<wchar_t>;

  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
    class basic_stringstream;
  using stringstream  = basic_stringstream<char>;
  using wstringstream = basic_stringstream<wchar_t>;
}
```

**1** The header `<sstream>` defines four class templates and eight types that associate stream buffers with objects of class `basic_string`, as described in [[string.classes]](../strings/string.classes.md).
