# 26.7.4.3 Access functions [slice.access]

[Contents](../README.md) · up: [26.7.4 Class `slice` \[class.slice\]](class.slice.md) · prev: [26.7.4.2 Constructors \[cons.slice\]](cons.slice.md) · next: [26.7.4.4 Operators \[slice.ops\]](slice.ops.md)

```cpp
size_t start() const;
size_t size() const;
size_t stride() const;
```

**1** *Returns:* The start, length, or stride specified by a `slice` object.

**2** *Complexity:* Constant time.
