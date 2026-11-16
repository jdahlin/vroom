# 20.9.2.2 Members [bitset.members]

[Contents](../README.md) · up: [20.9.2 Class template `bitset` \[template.bitset\]](template.bitset.md) · prev: [20.9.2.1 Constructors \[bitset.cons\]](bitset.cons.md) · next: [20.9.3 `bitset` hash support \[bitset.hash\]](bitset.hash.md)

```cpp
bitset<N>& operator&=(const bitset<N>& rhs) noexcept;
```

**1** *Effects:* Clears each bit in `*this` for which the corresponding bit in `rhs` is clear, and leaves all other bits unchanged.

**2** *Returns:* `*this`.

```cpp
bitset<N>& operator|=(const bitset<N>& rhs) noexcept;
```

**3** *Effects:* Sets each bit in `*this` for which the corresponding bit in `rhs` is set, and leaves all other bits unchanged.

**4** *Returns:* `*this`.

```cpp
bitset<N>& operator^=(const bitset<N>& rhs) noexcept;
```

**5** *Effects:* Toggles each bit in `*this` for which the corresponding bit in `rhs` is set, and leaves all other bits unchanged.

**6** *Returns:* `*this`.

```cpp
bitset<N>& operator<<=(size_t pos) noexcept;
```

**7** *Effects:* Replaces each bit at position `I` in `*this` with a value determined as follows:

- If `I < pos`, the new value is zero;
- If `I >= pos`, the new value is the previous value of the bit at position `I - pos`.

**8** *Returns:* `*this`.

```cpp
bitset<N>& operator>>=(size_t pos) noexcept;
```

**9** *Effects:* Replaces each bit at position `I` in `*this` with a value determined as follows:

- If `pos >= N - I`, the new value is zero;
- If `pos < N - I`, the new value is the previous value of the bit at position `I + pos`.

**10** *Returns:* `*this`.

```cpp
bitset<N>& set() noexcept;
```

**11** *Effects:* Sets all bits in `*this`.

**12** *Returns:* `*this`.

```cpp
bitset<N>& set(size_t pos, bool val = true);
```

**13** *Effects:* Stores a new value in the bit at position `pos` in `*this`. If `val` is `true`, the stored value is one, otherwise it is zero.

**14** *Returns:* `*this`.

**15** *Throws:* `out_of_range` if `pos` does not correspond to a valid bit position.

```cpp
bitset<N>& reset() noexcept;
```

**16** *Effects:* Resets all bits in `*this`.

**17** *Returns:* `*this`.

```cpp
bitset<N>& reset(size_t pos);
```

**18** *Effects:* Resets the bit at position `pos` in `*this`.

**19** *Returns:* `*this`.

**20** *Throws:* `out_of_range` if `pos` does not correspond to a valid bit position.

```cpp
bitset<N> operator~() const noexcept;
```

**21** *Effects:* Constructs an object `x` of class `bitset<N>` and initializes it with `*this`.

**22** *Returns:* `x.flip()`.

```cpp
bitset<N>& flip() noexcept;
```

**23** *Effects:* Toggles all bits in `*this`.

**24** *Returns:* `*this`.

```cpp
bitset<N>& flip(size_t pos);
```

**25** *Effects:* Toggles the bit at position `pos` in `*this`.

**26** *Returns:* `*this`.

**27** *Throws:* `out_of_range` if `pos` does not correspond to a valid bit position.

```cpp
unsigned long to_ulong() const;
```

**28** *Returns:* `x`.

**29** *Throws:* `overflow_error` if the integral value `x` corresponding to the bits in `*this` cannot be represented as type `unsigned long`.

```cpp
unsigned long long to_ullong() const;
```

**30** *Returns:* `x`.

**31** *Throws:* `overflow_error` if the integral value `x` corresponding to the bits in `*this` cannot be represented as type `unsigned long long`.

```cpp
template<class charT = char,
         class traits = char_traits<charT>,
         class Allocator = allocator<charT>>
  basic_string<charT, traits, Allocator>
    to_string(charT zero = charT('0'), charT one = charT('1')) const;
```

**32** *Effects:* Constructs a string object of the appropriate type and initializes it to a string of length `N` characters. Each character is determined by the value of its corresponding bit position in `*this`. Character position `N - 1` corresponds to bit position zero. Subsequent decreasing character positions correspond to increasing bit positions. Bit value zero becomes the character `zero`, bit value one becomes the character `one`.

**33** *Returns:* The created object.

```cpp
size_t count() const noexcept;
```

**34** *Returns:* A count of the number of bits set in `*this`.

```cpp
constexpr size_t size() const noexcept;
```

**35** *Returns:* `N`.

```cpp
bool operator==(const bitset<N>& rhs) const noexcept;
```

**36** *Returns:* `true` if the value of each bit in `*this` equals the value of the corresponding bit in `rhs`.

```cpp
bool test(size_t pos) const;
```

**37** *Returns:* `true` if the bit at position `pos` in `*this` has the value one.

**38** *Throws:* `out_of_range` if `pos` does not correspond to a valid bit position.

```cpp
bool all() const noexcept;
```

**39** *Returns:* `count() == size()`.

```cpp
bool any() const noexcept;
```

**40** *Returns:* `count() != 0`.

```cpp
bool none() const noexcept;
```

**41** *Returns:* `count() == 0`.

```cpp
bitset<N> operator<<(size_t pos) const noexcept;
```

**42** *Returns:* `bitset<N>(*this) <<= pos`.

```cpp
bitset<N> operator>>(size_t pos) const noexcept;
```

**43** *Returns:* `bitset<N>(*this) >>= pos`.

```cpp
constexpr bool operator[](size_t pos) const;
```

**44** *Preconditions:* `pos` is valid.

**45** *Returns:* `true` if the bit at position `pos` in `*this` has the value one, otherwise `false`.

**46** *Throws:* Nothing.

```cpp
bitset<N>::reference operator[](size_t pos);
```

**47** *Preconditions:* `pos` is valid.

**48** *Returns:* An object of type `bitset<N>::reference` such that `(*this)[pos] == this->test(pos)`, and such that `(*this)[pos] = val` is equivalent to `this->set(pos, val)`.

**49** *Throws:* Nothing.

**50** *Remarks:* For the purpose of determining the presence of a data race ([[intro.multithread]](../intro.multithread.md)), any access or update through the resulting reference potentially accesses or modifies, respectively, the entire underlying bitset.
