# 20.9.1 Header `<bitset>` synopsis [bitset.syn]

[Contents](../README.md) · up: [20.9 Bitsets \[bitset\]](bitset.md) · prev: [20.9 Bitsets \[bitset\]](bitset.md) · next: [20.9.2 Class template `bitset` \[template.bitset\]](template.bitset.md)

**1** The header `<bitset>` defines a class template and several related functions for representing and manipulating fixed-size sequences of bits.

```cpp
#include <string>
#include <iosfwd>   // for istream ([istream.syn]), ostream ([ostream.syn]), see [iosfwd.syn]

namespace std {
  template<size_t N> class bitset;

  // [bitset.operators], bitset operators
  template<size_t N>
    bitset<N> operator&(const bitset<N>&, const bitset<N>&) noexcept;
  template<size_t N>
    bitset<N> operator|(const bitset<N>&, const bitset<N>&) noexcept;
  template<size_t N>
    bitset<N> operator^(const bitset<N>&, const bitset<N>&) noexcept;
  template<class charT, class traits, size_t N>
    basic_istream<charT, traits>&
      operator>>(basic_istream<charT, traits>& is, bitset<N>& x);
  template<class charT, class traits, size_t N>
    basic_ostream<charT, traits>&
      operator<<(basic_ostream<charT, traits>& os, const bitset<N>& x);
}
```
