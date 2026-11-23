# 22.3.9.3 Iterators [forwardlist.iter]

[Contents](../../README.md) · up: [22.3.9 Class template `forward_list` \[forwardlist\]](forwardlist.md) · prev: [22.3.9.2 Constructors, copy, and assignment \[forwardlist.cons\]](forwardlist.cons.md) · next: [22.3.9.4 Element access \[forwardlist.access\]](forwardlist.access.md)

```cpp
iterator before_begin() noexcept;
const_iterator before_begin() const noexcept;
const_iterator cbefore_begin() const noexcept;
```

**1** *Returns:* A non-dereferenceable iterator that, when incremented, is equal to the iterator returned by `begin()`.

**2** *Effects:* `cbefore_begin()` is equivalent to `const_cast<forward_list const&>(*this).before_begin()`.

**3** *Remarks:* `before_begin() == end()` shall equal `false`.
