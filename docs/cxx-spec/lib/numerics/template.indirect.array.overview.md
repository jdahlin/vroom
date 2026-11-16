# 26.7.9.1 Overview [template.indirect.array.overview]

[Contents](../../README.md) · up: [26.7.9 Class template `indirect_array` \[template.indirect.array\]](template.indirect.array.md) · prev: [26.7.9 Class template `indirect_array` \[template.indirect.array\]](template.indirect.array.md) · next: [26.7.9.2 Assignment \[indirect.array.assign\]](indirect.array.assign.md)

```cpp
namespace std {
  template<class T> class indirect_array {
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

    indirect_array(const indirect_array&);
    ~indirect_array();
    const indirect_array& operator=(const indirect_array&) const;
    void operator=(const T&) const;

    indirect_array() = delete;  // as implied by declaring copy constructor above
  };
}
```

**1** This template is a helper template used by the indirect subscript operator

```cpp
indirect_array<T> valarray<T>::operator[](const valarray<size_t>&).
```

**2** It has reference semantics to a subset of an array specified by an `indirect_array`. Thus, the expression `a[indirect] = b;` has the effect of assigning the elements of `b` to the elements in `a` whose indices appear in `indirect`.
