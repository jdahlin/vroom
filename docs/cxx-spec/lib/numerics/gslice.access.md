# 26.7.6.3 Access functions [gslice.access]

[Contents](../../README.md) · up: [26.7.6 The `gslice` class \[class.gslice\]](class.gslice.md) · prev: [26.7.6.2 Constructors \[gslice.cons\]](gslice.cons.md) · next: [26.7.7 Class template `gslice_array` \[template.gslice.array\]](template.gslice.array.md)

```cpp
size_t           start()  const;
valarray<size_t> size() const;
valarray<size_t> stride() const;
```

**1** *Returns:* The representation of the start, lengths, or strides specified for the `gslice`.

**2** *Complexity:* `start()` is constant time. `size()` and `stride()` are linear in the number of strides.
