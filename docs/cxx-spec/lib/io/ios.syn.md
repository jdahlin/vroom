# 29.5.1 Header `<ios>` synopsis [ios.syn]

[Contents](../../README.md) · up: [29.5 Iostreams base classes \[iostreams.base\]](iostreams.base.md) · prev: [29.5 Iostreams base classes \[iostreams.base\]](iostreams.base.md) · next: [29.5.2 Types \[stream.types\]](stream.types.md)

```cpp
#include <iosfwd>   // see [iosfwd.syn]

namespace std {
  using streamoff  = implementation-defined;
  using streamsize = implementation-defined;
  template<class stateT> class fpos;

  class ios_base;
  template<class charT, class traits = char_traits<charT>>
    class basic_ios;

  // [std.ios.manip], manipulators
  ios_base& boolalpha  (ios_base& str);
  ios_base& noboolalpha(ios_base& str);

  ios_base& showbase   (ios_base& str);
  ios_base& noshowbase (ios_base& str);

  ios_base& showpoint  (ios_base& str);
  ios_base& noshowpoint(ios_base& str);

  ios_base& showpos    (ios_base& str);
  ios_base& noshowpos  (ios_base& str);

  ios_base& skipws     (ios_base& str);
  ios_base& noskipws   (ios_base& str);

  ios_base& uppercase  (ios_base& str);
  ios_base& nouppercase(ios_base& str);

  ios_base& unitbuf    (ios_base& str);
  ios_base& nounitbuf  (ios_base& str);

  // [adjustfield.manip], adjustfield
  ios_base& internal   (ios_base& str);
  ios_base& left       (ios_base& str);
  ios_base& right      (ios_base& str);

  // [basefield.manip], basefield
  ios_base& dec        (ios_base& str);
  ios_base& hex        (ios_base& str);
  ios_base& oct        (ios_base& str);

  // [floatfield.manip], floatfield
  ios_base& fixed      (ios_base& str);
  ios_base& scientific (ios_base& str);
  ios_base& hexfloat   (ios_base& str);
  ios_base& defaultfloat(ios_base& str);

  // [error.reporting], error reporting
  enum class io_errc {
    stream = 1
  };

  template<> struct is_error_code_enum<io_errc> : public true_type { };
  error_code make_error_code(io_errc e) noexcept;
  error_condition make_error_condition(io_errc e) noexcept;
  const error_category& iostream_category() noexcept;
}
```
