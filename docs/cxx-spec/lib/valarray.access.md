# 26.7.2.4 Element access [valarray.access]

[Contents](../README.md) · up: [26.7.2 Class template `valarray` \[template.valarray\]](template.valarray.md) · prev: [26.7.2.3 Assignment \[valarray.assign\]](valarray.assign.md) · next: [26.7.2.5 Subset operations \[valarray.sub\]](valarray.sub.md)

```cpp
const T&  operator[](size_t n) const;
T& operator[](size_t n);
```

**1** *Preconditions:* `n < size()` is `true`.

**2** *Returns:* A reference to the corresponding element of the array. [*Note 1*: The expression `(a[i] = q, a[i]) == q` evaluates to `true` for any non-constant `valarray<T> a`, any `T q`, and for any `size_t i` such that the value of `i` is less than the length of `a`. — *end note*]

**3** *Remarks:* The expression `addressof(a[i+j]) == addressof(a[i]) + j` evaluates to `true` for all `size_t i` and `size_t j` such that `i+j < a.size()`.

**4** The expression `addressof(a[i]) != addressof(b[j])` evaluates to `true` for any two arrays `a` and `b` and for any `size_t i` and `size_t j` such that `i < a.size()` and `j < b.size()`. [*Note 2*: This property indicates an absence of aliasing and may be used to advantage by optimizing compilers. Compilers may take advantage of inlining, constant propagation, loop fusion, tracking of pointers obtained from `operator new`, and other techniques to generate efficient `valarray`s. — *end note*]

**5** The reference returned by the subscript operator for an array shall be valid until the member function `resize(size_t, T)` ([[valarray.members]](valarray.members.md)) is called for that array or until the lifetime of that array ends, whichever happens first.
