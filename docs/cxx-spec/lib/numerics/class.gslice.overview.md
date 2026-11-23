# 26.7.6.1 Overview [class.gslice.overview]

[Contents](../../README.md) · up: [26.7.6 The `gslice` class \[class.gslice\]](class.gslice.md) · prev: [26.7.6 The `gslice` class \[class.gslice\]](class.gslice.md) · next: [26.7.6.2 Constructors \[gslice.cons\]](gslice.cons.md)

```cpp
namespace std {
  class gslice {
  public:
    gslice();
    gslice(size_t s, const valarray<size_t>& l, const valarray<size_t>& d);

    size_t           start() const;
    valarray<size_t> size() const;
    valarray<size_t> stride() const;
  };
}
```

**1** This class represents a generalized slice out of an array. A `gslice` is defined by a starting offset (*s*), a set of lengths (*l*<sub>*j*</sub>), and a set of strides (*d*<sub>*j*</sub>). The number of lengths shall equal the number of strides.

**2** A `gslice` represents a mapping from a set of indices (*i*<sub>*j*</sub>), equal in number to the number of strides, to a single index *k*. It is useful for building multidimensional array classes using the `valarray` template, which is one-dimensional. The set of one-dimensional index values specified by a `gslice` are

$$k = s + \sum_j i_j d_j$$

where the multidimensional indices *i*<sub>*j*</sub> range in value from 0 to *l*<sub>*ij*</sub> - 1.

**3** [*Example 1*: The `gslice` specification

```cpp
start  = 3
length = {2, 4, 3}
stride = {19, 4, 1}
```

yields the sequence of one-dimensional indices

$$k = 3 + (0, 1) \times 19 + (0, 1, 2, 3) \times 4 + (0, 1, 2) \times 1$$

which are ordered as shown in the following table:

(*i*<sub>0</sub>, *i*<sub>1</sub>, *i*<sub>2</sub>, *k*) =<br>(0, 0, 0, 3),<br>(0, 0, 1, 4),<br>(0, 0, 2, 5),<br>(0, 1, 0, 7),<br>(0, 1, 1, 8),<br>(0, 1, 2, 9),<br>(0, 2, 0, 11),<br>(0, 2, 1, 12),<br>(0, 2, 2, 13),<br>(0, 3, 0, 15),<br>(0, 3, 1, 16),<br>(0, 3, 2, 17),<br>(1, 0, 0, 22),<br>(1, 0, 1, 23),<br>…<br>(1, 3, 2, 36)

That is, the highest-ordered index turns fastest. — *end example*]

**4** It is possible to have degenerate generalized slices in which an address is repeated.

**5** [*Example 2*: If the stride parameters in the previous example are changed to {1, 1, 1}, the first few elements of the resulting sequence of indices will be

(0, 0, 0, 3),<br>(0, 0, 1, 4),<br>(0, 0, 2, 5),<br>(0, 1, 0, 4),<br>(0, 1, 1, 5),<br>(0, 1, 2, 6),<br>…

— *end example*]

**6** If a degenerate slice is used as the argument to the non-`const` version of `operator[](const gslice&)`, the behavior is undefined.
