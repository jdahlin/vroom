# D.21 Deprecated convenience conversion interfaces [depr.conversions]

[Contents](README.md) · up: [Annex D Compatibility features \[depr\]](depr.md) · prev: [D.20.2 Requirements \[depr.locale.stdcvt.req\]](depr.locale.stdcvt.req.md) · next: [D.21.1 Class template `wstring_convert` \[depr.conversions.string\]](depr.conversions.string.md)

**1** The header `<locale>` ([[locale.syn]](lib/locale.syn.md)) has the following additions:

```cpp
namespace std {
  template<class Codecvt, class Elem = wchar_t,
           class WideAlloc = allocator<Elem>,
           class ByteAlloc = allocator<char>>
    class wstring_convert;

  template<class Codecvt, class Elem = wchar_t,
           class Tr = char_traits<Elem>>
    class wbuffer_convert;
}
```

## Subclauses

- [D.21.1 Class template `wstring_convert` \[depr.conversions.string\]](depr.conversions.string.md)
- [D.21.2 Class template `wbuffer_convert` \[depr.conversions.buffer\]](depr.conversions.buffer.md)
