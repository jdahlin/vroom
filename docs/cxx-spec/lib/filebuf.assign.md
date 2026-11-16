# 29.9.2.2 Assignment and swap [filebuf.assign]

[Contents](../README.md) · up: [29.9.2 Class template `basic_filebuf` \[filebuf\]](filebuf.md) · prev: [29.9.2.1 Constructors \[filebuf.cons\]](filebuf.cons.md) · next: [29.9.2.3 Member functions \[filebuf.members\]](filebuf.members.md)

```cpp
basic_filebuf& operator=(basic_filebuf&& rhs);
```

**1** *Effects:* Calls `close()` then move assigns from `rhs`. After the move assignment `*this` has the observable state it would have had if it had been move constructed from `rhs` (see [[filebuf.cons]](filebuf.cons.md)).

**2** *Returns:* `*this`.

```cpp
void swap(basic_filebuf& rhs);
```

**3** *Effects:* Exchanges the state of `*this` and `rhs`.

```cpp
template<class charT, class traits>
  void swap(basic_filebuf<charT, traits>& x,
            basic_filebuf<charT, traits>& y);
```

**4** *Effects:* Equivalent to: `x.swap(y)`.
