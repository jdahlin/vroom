# 20.9.2.1 Constructors [bitset.cons]

[Contents](../README.md) · up: [20.9.2 Class template `bitset` \[template.bitset\]](template.bitset.md) · prev: [20.9.2 Class template `bitset` \[template.bitset\]](template.bitset.md) · next: [20.9.2.2 Members \[bitset.members\]](bitset.members.md)

```cpp
constexpr bitset() noexcept;
```

**1** *Effects:* Initializes all bits in `*this` to zero.

```cpp
constexpr bitset(unsigned long long val) noexcept;
```

**2** *Effects:* Initializes the first `M` bit positions to the corresponding bit values in `val`. `M` is the smaller of `N` and the number of bits in the value representation ([[basic.types]](../basic.types.md)) of `unsigned long long`. If `M < N`, the remaining bit positions are initialized to zero.

```cpp
template<class charT, class traits, class Allocator>
  explicit bitset(
    const basic_string<charT, traits, Allocator>& str,
    typename basic_string<charT, traits, Allocator>::size_type pos = 0,
    typename basic_string<charT, traits, Allocator>::size_type n
      = basic_string<charT, traits, Allocator>::npos,
    charT zero = charT('0'),
    charT one = charT('1'));
```

**3** *Effects:* Determines the effective length `rlen` of the initializing string as the smaller of `n` and `str.size() - pos`. Initializes the first `M` bit positions to values determined from the corresponding characters in the string `str`. `M` is the smaller of `N` and `rlen`.

**4** An element of the constructed object has value zero if the corresponding character in `str`, beginning at position `pos`, is `zero`. Otherwise, the element has the value one. Character position `pos + M - 1` corresponds to bit position zero. Subsequent decreasing character positions correspond to increasing bit positions.

**5** If `M < N`, remaining bit positions are initialized to zero.

**6** The function uses `traits::eq` to compare the character values.

**7** *Throws:* `out_of_range` if `pos > str.size()` or `invalid_argument` if any of the `rlen` characters in `str` beginning at position `pos` is other than `zero` or `one`.

```cpp
template<class charT>
  explicit bitset(
    const charT* str,
    typename basic_string<charT>::size_type n = basic_string<charT>::npos,
    charT zero = charT('0'),
    charT one = charT('1'));
```

**8** *Effects:* As if by:

```cpp
bitset(n == basic_string<charT>::npos
          ? basic_string<charT>(str)
          : basic_string<charT>(str, n),
       0, n, zero, one)
```
