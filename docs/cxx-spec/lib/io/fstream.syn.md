# 29.9.1 Header `<fstream>` synopsis [fstream.syn]

[Contents](../../README.md) · up: [29.9 File-based streams \[file.streams\]](file.streams.md) · prev: [29.9 File-based streams \[file.streams\]](file.streams.md) · next: [29.9.2 Class template `basic_filebuf` \[filebuf\]](filebuf.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
    class basic_filebuf;
  using filebuf  = basic_filebuf<char>;
  using wfilebuf = basic_filebuf<wchar_t>;

  template<class charT, class traits = char_traits<charT>>
    class basic_ifstream;
  using ifstream  = basic_ifstream<char>;
  using wifstream = basic_ifstream<wchar_t>;

  template<class charT, class traits = char_traits<charT>>
    class basic_ofstream;
  using ofstream  = basic_ofstream<char>;
  using wofstream = basic_ofstream<wchar_t>;

  template<class charT, class traits = char_traits<charT>>
    class basic_fstream;
  using fstream  = basic_fstream<char>;
  using wfstream = basic_fstream<wchar_t>;
}
```

**1** The header `<fstream>` defines four class templates and eight types that associate stream buffers with files and assist reading and writing files.

**2** [*Note 1*: The class template `basic_filebuf` treats a file as a source or sink of bytes. In an environment that uses a large character set, the file typically holds multibyte character sequences and the `basic_filebuf` object converts those multibyte sequences into wide character sequences. — *end note*]

**3** In subclause [[file.streams]](file.streams.md), member functions taking arguments of `const filesystem::path::value_type*` are only be provided on systems where `filesystem::path::value_type` ([[fs.class.path]](fs.class.path.md)) is not `char`. [*Note 2*: These functions enable class `path` support for systems with a wide native path character type, such as `wchar_t`. — *end note*]
