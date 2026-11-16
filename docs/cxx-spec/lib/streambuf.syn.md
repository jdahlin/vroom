# 29.6.1 Header `<streambuf>` synopsis [streambuf.syn]

[Contents](../README.md) · up: [29.6 Stream buffers \[stream.buffers\]](stream.buffers.md) · prev: [29.6 Stream buffers \[stream.buffers\]](stream.buffers.md) · next: [29.6.2 Stream buffer requirements \[streambuf.reqts\]](streambuf.reqts.md)

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>>
    class basic_streambuf;
  using streambuf  = basic_streambuf<char>;
  using wstreambuf = basic_streambuf<wchar_t>;
}
```

**1** The header `<streambuf>` defines types that control input from and output to *character* sequences.
