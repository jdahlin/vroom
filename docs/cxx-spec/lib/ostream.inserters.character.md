# 29.7.5.2.4 Character inserter function templates [ostream.inserters.character]

[Contents](../README.md) · up: [29.7.5.2 Formatted output functions \[ostream.formatted\]](ostream.formatted.md) · prev: [29.7.5.2.3 `basic_ostream::operator<<` \[ostream.inserters\]](ostream.inserters.md) · next: [29.7.5.3 Unformatted output functions \[ostream.unformatted\]](ostream.unformatted.md)

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>& out, charT c);
template<class charT, class traits>
  basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>& out, char c);
// specialization
template<class traits>
  basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>& out, char c);
// signed and unsigned
template<class traits>
  basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>& out, signed char c);
template<class traits>
  basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>& out, unsigned char c);
```

**1** *Effects:* Behaves as a formatted output function ([[ostream.formatted.reqmts]](ostream.formatted.reqmts.md)) of `out`. Constructs a character sequence `seq`. If `c` has type `char` and the character type of the stream is not `char`, then `seq` consists of `out.widen(c)`; otherwise `seq` consists of `c`. Determines padding for `seq` as described in [[ostream.formatted.reqmts]](ostream.formatted.reqmts.md). Inserts `seq` into `out`. Calls `os.width(0)`.

**2** *Returns:* `out`.

```cpp
template<class charT, class traits>
  basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>& out, const charT* s);
template<class charT, class traits>
  basic_ostream<charT, traits>& operator<<(basic_ostream<charT, traits>& out, const char* s);
template<class traits>
  basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>& out, const char* s);
template<class traits>
  basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>& out, const signed char* s);
template<class traits>
  basic_ostream<char, traits>& operator<<(basic_ostream<char, traits>& out,
                                          const unsigned char* s);
```

**3** *Preconditions:* `s` is not a null pointer.

**4** *Effects:* Behaves like a formatted inserter (as described in [[ostream.formatted.reqmts]](ostream.formatted.reqmts.md)) of `out`. Creates a character sequence `seq` of `n` characters starting at `s`, each widened using `out.widen()` ([[basic.ios.members]](basic.ios.members.md)), where `n` is the number that would be computed as if by:

- `traits::length(s)` for the overload where the first argument is of type `basic_ostream<charT, traits>&` and the second is of type `const charT*`, and also for the overload where the first argument is of type `basic_ostream<char, traits>&` and the second is of type `const char*`,
- `char_traits<char>::length(s)` for the overload where the first argument is of type `basic_ostream<charT, traits>&` and the second is of type `const char*`,
- `traits::length(reinterpret_cast<const char*>(s))` for the other two overloads.

Determines padding for `seq` as described in [[ostream.formatted.reqmts]](ostream.formatted.reqmts.md). Inserts `seq` into `out`. Calls `width(0)`.

**5** *Returns:* `out`.
