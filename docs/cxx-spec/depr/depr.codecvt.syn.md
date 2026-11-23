# D.20.1 Header `<codecvt>` synopsis [depr.codecvt.syn]

[Contents](../README.md) · up: [D.20 Deprecated standard code conversion facets \[depr.locale.stdcvt\]](depr.locale.stdcvt.md) · prev: [D.20 Deprecated standard code conversion facets \[depr.locale.stdcvt\]](depr.locale.stdcvt.md) · next: [D.20.2 Requirements \[depr.locale.stdcvt.req\]](depr.locale.stdcvt.req.md)

```cpp
namespace std {
  enum codecvt_mode {
    consume_header = 4,
    generate_header = 2,
    little_endian = 1
  };

  template<class Elem, unsigned long Maxcode = 0x10ffff, codecvt_mode Mode = (codecvt_mode)0>
    class codecvt_utf8 : public codecvt<Elem, char, mbstate_t> {
    public:
      explicit codecvt_utf8(size_t refs = 0);
      ~codecvt_utf8();
    };

  template<class Elem, unsigned long Maxcode = 0x10ffff, codecvt_mode Mode = (codecvt_mode)0>
    class codecvt_utf16 : public codecvt<Elem, char, mbstate_t> {
    public:
      explicit codecvt_utf16(size_t refs = 0);
      ~codecvt_utf16();
    };

  template<class Elem, unsigned long Maxcode = 0x10ffff, codecvt_mode Mode = (codecvt_mode)0>
    class codecvt_utf8_utf16 : public codecvt<Elem, char, mbstate_t> {
    public:
      explicit codecvt_utf8_utf16(size_t refs = 0);
      ~codecvt_utf8_utf16();
    };
}
```
