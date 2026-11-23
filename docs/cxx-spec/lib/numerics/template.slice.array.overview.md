# 26.7.5.1 Overview [template.slice.array.overview]

[Contents](../../README.md) · up: [26.7.5 Class template `slice_array` \[template.slice.array\]](template.slice.array.md) · prev: [26.7.5 Class template `slice_array` \[template.slice.array\]](template.slice.array.md) · next: [26.7.5.2 Assignment \[slice.arr.assign\]](slice.arr.assign.md)

```cpp
namespace std {
  template<class T> class slice_array {
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

    slice_array(const slice_array&);
    ~slice_array();
    const slice_array& operator=(const slice_array&) const;
    void operator=(const T&) const;

    slice_array() = delete;     // as implied by declaring copy constructor above
  };
}
```

**1** This template is a helper template used by the `slice` subscript operator

```cpp
slice_array<T> valarray<T>::operator[](slice);
```

**2** It has reference semantics to a subset of an array specified by a `slice` object. [*Example 1*: The expression `a[slice(1, 5, 3)] = b;` has the effect of assigning the elements of `b` to a slice of the elements in `a`. For the slice shown, the elements selected from `a` are 1, 4, …, 13. — *end example*]
