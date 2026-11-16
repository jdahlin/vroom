# 26.7.7.1 Overview [template.gslice.array.overview]

[Contents](../../README.md) · up: [26.7.7 Class template `gslice_array` \[template.gslice.array\]](template.gslice.array.md) · prev: [26.7.7 Class template `gslice_array` \[template.gslice.array\]](template.gslice.array.md) · next: [26.7.7.2 Assignment \[gslice.array.assign\]](gslice.array.assign.md)

```cpp
namespace std {
  template<class T> class gslice_array {
  public:
    using value_type = T;

    void operator=  (const valarray<T>&) const;
    void operator*= (const valarray<T>&) const;
    void operator/= (const valarray<T>&) const;
    void operator%= (const valarray<T>&) const;
    void operator+= (const valarray<T>&) const;
    void operator-= (const valarray<T>&) const;
    void operator^= (const valarray<T>&) const;
    void operator&= (const valarray<T>&) const;
    void operator|= (const valarray<T>&) const;
    void operator<<=(const valarray<T>&) const;
    void operator>>=(const valarray<T>&) const;

    gslice_array(const gslice_array&);
    ~gslice_array();
    const gslice_array& operator=(const gslice_array&) const;
    void operator=(const T&) const;

    gslice_array() = delete;    // as implied by declaring copy constructor above
  };
}
```

**1** This template is a helper template used by the `gslice` subscript operator

```cpp
gslice_array<T> valarray<T>::operator[](const gslice&);
```

**2** It has reference semantics to a subset of an array specified by a `gslice` object. Thus, the expression `a[gslice(1, length, stride)] = b` has the effect of assigning the elements of `b` to a generalized slice of the elements in `a`.
