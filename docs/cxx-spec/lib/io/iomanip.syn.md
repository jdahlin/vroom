# 29.7.3 Header `<iomanip>` synopsis [iomanip.syn]

[Contents](../../README.md) · up: [29.7 Formatting and manipulators \[iostream.format\]](iostream.format.md) · prev: [29.7.2 Header `<ostream>` synopsis \[ostream.syn\]](ostream.syn.md) · next: [29.7.4 Input streams \[input.streams\]](input.streams.md)

```cpp
namespace std {
  // types T1, T2, ... are unspecified implementation types
  T1 resetiosflags(ios_base::fmtflags mask);
  T2 setiosflags  (ios_base::fmtflags mask);
  T3 setbase(int base);
  template<class charT> T4 setfill(charT c);
  T5 setprecision(int n);
  T6 setw(int n);
  template<class moneyT> T7 get_money(moneyT& mon, bool intl = false);
  template<class moneyT> T8 put_money(const moneyT& mon, bool intl = false);
  template<class charT> T9 get_time(struct tm* tmb, const charT* fmt);
  template<class charT> T10 put_time(const struct tm* tmb, const charT* fmt);

  template<class charT>
    T11 quoted(const charT* s, charT delim = charT('"'), charT escape = charT('\\'));

  template<class charT, class traits, class Allocator>
    T12 quoted(const basic_string<charT, traits, Allocator>& s,
               charT delim = charT('"'), charT escape = charT('\\'));

  template<class charT, class traits, class Allocator>
    T13 quoted(basic_string<charT, traits, Allocator>& s,
               charT delim = charT('"'), charT escape = charT('\\'));

  template<class charT, class traits>
    T14 quoted(basic_string_view<charT, traits> s,
               charT delim = charT('"'), charT escape = charT('\\'));
}
```
