# 29.8.2.2 Assignment and swap [stringbuf.assign]

[Contents](../../README.md) · up: [29.8.2 Class template `basic_stringbuf` \[stringbuf\]](stringbuf.md) · prev: [29.8.2.1 Constructors \[stringbuf.cons\]](stringbuf.cons.md) · next: [29.8.2.3 Member functions \[stringbuf.members\]](stringbuf.members.md)

```cpp
basic_stringbuf& operator=(basic_stringbuf&& rhs);
```

**1** *Effects:* After the move assignment `*this` has the observable state it would have had if it had been move constructed from `rhs` (see [[stringbuf.cons]](stringbuf.cons.md)).

**2** *Returns:* `*this`.

```cpp
void swap(basic_stringbuf& rhs) noexcept(see below);
```

**3** *Preconditions:* `allocator_traits<Allocator>::propagate_on_container_swap::value` is `true` or `get_allocator() == s.get_allocator()` is `true`.

**4** *Effects:* Exchanges the state of `*this` and `rhs`.

**5** *Remarks:* The expression inside `noexcept` is equivalent to:<br> `allocator_traits<Allocator>::propagate_on_container_swap::value ||`<br> `allocator_traits<Allocator>::is_always_equal::value`.

```cpp
template<class charT, class traits, class Allocator>
  void swap(basic_stringbuf<charT, traits, Allocator>& x,
            basic_stringbuf<charT, traits, Allocator>& y) noexcept(noexcept(x.swap(y)));
```

**6** *Effects:* Equivalent to: `x.swap(y)`.
