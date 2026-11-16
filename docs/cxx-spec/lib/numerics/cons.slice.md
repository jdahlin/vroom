# 26.7.4.2 Constructors [cons.slice]

[Contents](../../README.md) · up: [26.7.4 Class `slice` \[class.slice\]](class.slice.md) · prev: [26.7.4.1 Overview \[class.slice.overview\]](class.slice.overview.md) · next: [26.7.4.3 Access functions \[slice.access\]](slice.access.md)

```cpp
slice();
slice(size_t start, size_t length, size_t stride);
slice(const slice&);
```

**1** The default constructor is equivalent to `slice(0, 0, 0)`. A default constructor is provided only to permit the declaration of arrays of slices. The constructor with arguments for a slice takes a start, length, and stride parameter.

**2** [*Example 1*: `slice(3, 8, 2)` constructs a slice which selects elements 3, 5, 7, …, 17 from an array. — *end example*]
