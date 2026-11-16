# 26.7.8.1 Overview [template.mask.array.overview]

[Contents](../README.md) · up: [26.7.8 Class template `mask_array` \[template.mask.array\]](template.mask.array.md) · prev: [26.7.8 Class template `mask_array` \[template.mask.array\]](template.mask.array.md) · next: [26.7.8.2 Assignment \[mask.array.assign\]](mask.array.assign.md)

```cpp
namespace std {
  template<class T> class mask_array {
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

    mask_array(const mask_array&);
    ~mask_array();
    const mask_array& operator=(const mask_array&) const;
    void operator=(const T&) const;

    mask_array() = delete;      // as implied by declaring copy constructor above
  };
}
```

**1** This template is a helper template used by the mask subscript operator:

```cpp
mask_array<T> valarray<T>::operator[](const valarray<bool>&).
```

**2** It has reference semantics to a subset of an array specified by a boolean mask. Thus, the expression `a[mask] = b;` has the effect of assigning the elements of `b` to the masked elements in `a` (those for which the corresponding element in `mask` is `true`).
