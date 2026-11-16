# 26.7.4.1 Overview [class.slice.overview]

[Contents](../README.md) · up: [26.7.4 Class `slice` \[class.slice\]](class.slice.md) · prev: [26.7.4 Class `slice` \[class.slice\]](class.slice.md) · next: [26.7.4.2 Constructors \[cons.slice\]](cons.slice.md)

```cpp
namespace std {
  class slice {
  public:
    slice();
    slice(size_t, size_t, size_t);

    size_t start() const;
    size_t size() const;
    size_t stride() const;

    friend bool operator==(const slice& x, const slice& y);
  };
}
```

**1** The `slice` class represents a BLAS-like slice from an array. Such a slice is specified by a starting index, a length, and a stride.[^1]

[^1]: BLAS stands for *Basic Linear Algebra Subprograms.* C++ programs may instantiate this class. See, for example, Dongarra, Du Croz, Duff, and Hammerling: *A set of Level 3 Basic Linear Algebra Subprograms*; Technical Report MCS-P1-0888, Argonne National Laboratory (USA), Mathematics and Computer Science Division, August, 1988.
