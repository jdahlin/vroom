# 20.9.2 Class template `bitset` [template.bitset]

[Contents](../README.md) · up: [20.9 Bitsets \[bitset\]](bitset.md) · prev: [20.9.1 Header `<bitset>` synopsis \[bitset.syn\]](bitset.syn.md) · next: [20.9.2.1 Constructors \[bitset.cons\]](bitset.cons.md)

```cpp
namespace std {
  template<size_t N> class bitset {
  public:
    // bit reference
    class reference {
      friend class bitset;
      reference() noexcept;

    public:
      reference(const reference&) = default;
      ~reference();
      reference& operator=(bool x) noexcept;            // for b[i] = x;
      reference& operator=(const reference&) noexcept;  // for b[i] = b[j];
      bool operator~() const noexcept;                  // flips the bit
      operator bool() const noexcept;                   // for x = b[i];
      reference& flip() noexcept;                       // for b[i].flip();
    };

    // [bitset.cons], constructors
    constexpr bitset() noexcept;
    constexpr bitset(unsigned long long val) noexcept;
    template<class charT, class traits, class Allocator>
      explicit bitset(
        const basic_string<charT, traits, Allocator>& str,
        typename basic_string<charT, traits, Allocator>::size_type pos = 0,
        typename basic_string<charT, traits, Allocator>::size_type n
          = basic_string<charT, traits, Allocator>::npos,
        charT zero = charT('0'),
        charT one = charT('1'));
    template<class charT>
      explicit bitset(
        const charT* str,
        typename basic_string<charT>::size_type n = basic_string<charT>::npos,
        charT zero = charT('0'),
        charT one = charT('1'));

    // [bitset.members], bitset operations
    bitset<N>& operator&=(const bitset<N>& rhs) noexcept;
    bitset<N>& operator|=(const bitset<N>& rhs) noexcept;
    bitset<N>& operator^=(const bitset<N>& rhs) noexcept;
    bitset<N>& operator<<=(size_t pos) noexcept;
    bitset<N>& operator>>=(size_t pos) noexcept;
    bitset<N>& set() noexcept;
    bitset<N>& set(size_t pos, bool val = true);
    bitset<N>& reset() noexcept;
    bitset<N>& reset(size_t pos);
    bitset<N>  operator~() const noexcept;
    bitset<N>& flip() noexcept;
    bitset<N>& flip(size_t pos);

    // element access
    constexpr bool operator[](size_t pos) const;        // for b[i];
    reference operator[](size_t pos);                   // for b[i];

    unsigned long to_ulong() const;
    unsigned long long to_ullong() const;
    template<class charT = char,
             class traits = char_traits<charT>,
             class Allocator = allocator<charT>>
      basic_string<charT, traits, Allocator>
        to_string(charT zero = charT('0'), charT one = charT('1')) const;

    size_t count() const noexcept;
    constexpr size_t size() const noexcept;
    bool operator==(const bitset<N>& rhs) const noexcept;
    bool test(size_t pos) const;
    bool all() const noexcept;
    bool any() const noexcept;
    bool none() const noexcept;
    bitset<N> operator<<(size_t pos) const noexcept;
    bitset<N> operator>>(size_t pos) const noexcept;
  };

  // [bitset.hash], hash support
  template<class T> struct hash;
  template<size_t N> struct hash<bitset<N>>;
}
```

**1** The class template `bitset<N>` describes an object that can store a sequence consisting of a fixed number of bits, `N`.

**2** Each bit represents either the value zero (reset) or one (set). To *toggle* a bit is to change the value zero to one, or the value one to zero. Each bit has a non-negative position `pos`. When converting between an object of class `bitset<N>` and a value of some integral type, bit position `pos` corresponds to the *bit value* `1 << pos`. The integral value corresponding to two or more bits is the sum of their bit values.

**3** The functions described in this subclause can report three kinds of errors, each associated with a distinct exception:

- an *invalid-argument* error is associated with exceptions of type `invalid_argument` ([[invalid.argument]](invalid.argument.md));
- an *out-of-range* error is associated with exceptions of type `out_of_range` ([[out.of.range]](out.of.range.md));
- an *overflow* error is associated with exceptions of type `overflow_error` ([[overflow.error]](overflow.error.md)).

## Subclauses

- [20.9.2.1 Constructors \[bitset.cons\]](bitset.cons.md)
- [20.9.2.2 Members \[bitset.members\]](bitset.members.md)
