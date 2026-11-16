# 29.7.4.2.3 `basic_istream::operator>>` [istream.extractors]

[Contents](../../README.md) · up: [29.7.4.2 Formatted input functions \[istream.formatted\]](istream.formatted.md) · prev: [29.7.4.2.2 Arithmetic extractors \[istream.formatted.arithmetic\]](istream.formatted.arithmetic.md) · next: [29.7.4.3 Unformatted input functions \[istream.unformatted\]](istream.unformatted.md)

```cpp
basic_istream<charT, traits>&
  operator>>(basic_istream<charT, traits>& (*pf)(basic_istream<charT, traits>&));
```

**1** *Effects:* None. This extractor does not behave as a formatted input function (as described in [[istream.formatted.reqmts]](istream.formatted.reqmts.md)).

**2** *Returns:* `pf(*this)`.[^1]

```cpp
basic_istream<charT, traits>&
  operator>>(basic_ios<charT, traits>& (*pf)(basic_ios<charT, traits>&));
```

**3** *Effects:* Calls `pf(*this)`. This extractor does not behave as a formatted input function (as described in [[istream.formatted.reqmts]](istream.formatted.reqmts.md)).

**4** *Returns:* `*this`.

```cpp
basic_istream<charT, traits>& operator>>(ios_base& (*pf)(ios_base&));
```

**5** *Effects:* Calls `pf(*this)`.[^2] This extractor does not behave as a formatted input function (as described in [[istream.formatted.reqmts]](istream.formatted.reqmts.md)).

**6** *Returns:* `*this`.

```cpp
template<class charT, class traits, size_t N>
  basic_istream<charT, traits>& operator>>(basic_istream<charT, traits>& in, charT (&s)[N]);
template<class traits, size_t N>
  basic_istream<char, traits>& operator>>(basic_istream<char, traits>& in, unsigned char (&s)[N]);
template<class traits, size_t N>
  basic_istream<char, traits>& operator>>(basic_istream<char, traits>& in, signed char (&s)[N]);
```

**7** *Effects:* Behaves like a formatted input member (as described in [[istream.formatted.reqmts]](istream.formatted.reqmts.md)) of `in`. After a `sentry` object is constructed, `operator>>` extracts characters and stores them into `s`. If `width()` is greater than zero, `n` is `min(size_t(width()), N)`. Otherwise `n` is `N`. `n` is the maximum number of characters stored.

**8** Characters are extracted and stored until any of the following occurs:

- `n-1` characters are stored;
- end of file occurs on the input sequence;
- letting `ct` be `use_facet<ctype<charT>>(in.getloc())`, `ct.is(ct.space, c)` is `true`.

**9** `operator>>` then stores a null byte (`charT()`) in the next position, which may be the first position if no characters were extracted. `operator>>` then calls `width(0)`.

**10** If the function extracted no characters, it calls `setstate(failbit)`, which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md)).

**11** *Returns:* `in`.

```cpp
template<class charT, class traits>
  basic_istream<charT, traits>& operator>>(basic_istream<charT, traits>& in, charT& c);
template<class traits>
  basic_istream<char, traits>& operator>>(basic_istream<char, traits>& in, unsigned char& c);
template<class traits>
  basic_istream<char, traits>& operator>>(basic_istream<char, traits>& in, signed char& c);
```

**12** *Effects:* Behaves like a formatted input member (as described in [[istream.formatted.reqmts]](istream.formatted.reqmts.md)) of `in`. After a `sentry` object is constructed a character is extracted from `in`, if one is available, and stored in `c`. Otherwise, the function calls `in.setstate(failbit)`.

**13** *Returns:* `in`.

```cpp
basic_istream<charT, traits>& operator>>(basic_streambuf<charT, traits>* sb);
```

**14** *Effects:* Behaves as an unformatted input function ([[istream.unformatted]](istream.unformatted.md)). If `sb` is null, calls `setstate(failbit)`, which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md)). After a sentry object is constructed, extracts characters from `*this` and inserts them in the output sequence controlled by `sb`. Characters are extracted and inserted until any of the following occurs:

- end-of-file occurs on the input sequence;
- inserting in the output sequence fails (in which case the character to be inserted is not extracted);
- an exception occurs (in which case the exception is caught).

**15** If the function inserts no characters, it calls `setstate(failbit)`, which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md)). If it inserted no characters because it caught an exception thrown while extracting characters from `*this` and `failbit` is set in `exceptions()` ([[iostate.flags]](iostate.flags.md)), then the caught exception is rethrown.

**16** *Returns:* `*this`.

[^1]: See, for example, the function signature `ws(basic_istream&)` ([[istream.manip]](istream.manip.md)).
[^2]: See, for example, the function signature `dec(ios_base&)` ([[basefield.manip]](basefield.manip.md)).
