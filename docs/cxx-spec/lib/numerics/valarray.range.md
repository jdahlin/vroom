# 26.7.10 `valarray` range access [valarray.range]

[Contents](../../README.md) · up: [26.7 Numeric arrays \[numarray\]](numarray.md) · prev: [26.7.9.4 Fill function \[indirect.array.fill\]](indirect.array.fill.md) · next: [26.8 Mathematical functions for floating-point types \[c.math\]](c.math.md)

**1** In the `begin` and `end` function templates that follow, *unspecified*1 is a type that meets the requirements of a mutable *Cpp17RandomAccessIterator* ([[random.access.iterators]](../iterators/random.access.iterators.md)) and models `contiguous_iterator` ([[iterator.concept.contiguous]](../iterators/iterator.concept.contiguous.md)), whose `value_type` is the template parameter `T` and whose `reference` type is `T&`. *unspecified*2 is a type that meets the requirements of a constant *Cpp17RandomAccessIterator* and models `contiguous_iterator`, whose `value_type` is the template parameter `T` and whose `reference` type is `const T&`.

**2** The iterators returned by `begin` and `end` for an array are guaranteed to be valid until the member function `resize(size_t, T)` ([[valarray.members]](valarray.members.md)) is called for that array or until the lifetime of that array ends, whichever happens first.

```cpp
template<class T> unspecified1 begin(valarray<T>& v);
template<class T> unspecified2 begin(const valarray<T>& v);
```

**3** *Returns:* An iterator referencing the first value in the array.

```cpp
template<class T> unspecified1 end(valarray<T>& v);
template<class T> unspecified2 end(const valarray<T>& v);
```

**4** *Returns:* An iterator referencing one past the last value in the array.
