# 20.9.4 `bitset` operators [bitset.operators]

[Contents](../README.md) · up: [20.9 Bitsets \[bitset\]](bitset.md) · prev: [20.9.3 `bitset` hash support \[bitset.hash\]](bitset.hash.md) · next: [20.10 Memory \[memory\]](memory.md)

```cpp
bitset<N> operator&(const bitset<N>& lhs, const bitset<N>& rhs) noexcept;
```

**1** *Returns:* `bitset<N>(lhs) &= rhs`.

```cpp
bitset<N> operator|(const bitset<N>& lhs, const bitset<N>& rhs) noexcept;
```

**2** *Returns:* `bitset<N>(lhs) |= rhs`.

```cpp
bitset<N> operator^(const bitset<N>& lhs, const bitset<N>& rhs) noexcept;
```

**3** *Returns:* `bitset<N>(lhs) ^= rhs`.

```cpp
template<class charT, class traits, size_t N>
  basic_istream<charT, traits>&
    operator>>(basic_istream<charT, traits>& is, bitset<N>& x);
```

**4** A formatted input function ([[istream.formatted]](istream.formatted.md)).

**5** *Effects:* Extracts up to `N` characters from `is`. Stores these characters in a temporary object `str` of type `basic_string<charT, traits>`, then evaluates the expression `x = bitset<N>(str)`. Characters are extracted and stored until any of the following occurs:

- `N` characters have been extracted and stored;
- end-of-file occurs on the input sequence;
- the next input character is neither `is.widen('0')` nor `is.widen('1')` (in which case the input character is not extracted).

**6** If `N > 0` and no characters are stored in `str`, calls `is.setstate(ios_base::failbit)` (which may throw `ios_base::failure` ([[iostate.flags]](iostate.flags.md))).

**7** *Returns:* `is`.

```cpp
template<class charT, class traits, size_t N>
  basic_ostream<charT, traits>&
    operator<<(basic_ostream<charT, traits>& os, const bitset<N>& x);
```

**8** *Returns:*

```cpp
os << x.template to_string<charT, traits, allocator<charT>>(
  use_facet<ctype<charT>>(os.getloc()).widen('0'),
  use_facet<ctype<charT>>(os.getloc()).widen('1'))
```

(see [[ostream.formatted]](ostream.formatted.md)).
