# 21.5.5 Header `<cuchar>` synopsis [cuchar.syn]

[Contents](../../README.md) · up: [21.5 Null-terminated sequence utilities \[c.strings\]](c.strings.md) · prev: [21.5.4 Header `<cwchar>` synopsis \[cwchar.syn\]](cwchar.syn.md) · next: [21.5.6 Multibyte / wide string and character conversion functions \[c.mb.wcs\]](c.mb.wcs.md)

```cpp
namespace std {
  using mbstate_t = see below;
  using size_t = see [support.types.layout];

  size_t mbrtoc8(char8_t* pc8, const char* s, size_t n, mbstate_t* ps);
  size_t c8rtomb(char* s, char8_t c8, mbstate_t* ps);
  size_t mbrtoc16(char16_t* pc16, const char* s, size_t n, mbstate_t* ps);
  size_t c16rtomb(char* s, char16_t c16, mbstate_t* ps);
  size_t mbrtoc32(char32_t* pc32, const char* s, size_t n, mbstate_t* ps);
  size_t c32rtomb(char* s, char32_t c32, mbstate_t* ps);
}
```

**1** The contents and meaning of the header `<cuchar>` are the same as the C standard library header `<uchar.h>`, except that it declares the additional `mbrtoc8` and `c8rtomb` functions and does not declare types `char16_t` nor `char32_t`.

*See also:* ISO C 7.28
