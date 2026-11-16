# 26.7.6.2 Constructors [gslice.cons]

[Contents](../README.md) · up: [26.7.6 The `gslice` class \[class.gslice\]](class.gslice.md) · prev: [26.7.6.1 Overview \[class.gslice.overview\]](class.gslice.overview.md) · next: [26.7.6.3 Access functions \[gslice.access\]](gslice.access.md)

```cpp
gslice();
gslice(size_t start, const valarray<size_t>& lengths,
         const valarray<size_t>& strides);
gslice(const gslice&);
```

**1** The default constructor is equivalent to `gslice(0, valarray<size_t>(), valarray<size_t>())`. The constructor with arguments builds a `gslice` based on a specification of start, lengths, and strides, as explained in the previous subclause.
