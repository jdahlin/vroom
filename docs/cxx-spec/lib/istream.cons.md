# 29.7.4.1.1 Constructors [istream.cons]

[Contents](../README.md) · up: [29.7.4.1 Class template `basic_istream` \[istream\]](istream.md) · prev: [29.7.4.1 Class template `basic_istream` \[istream\]](istream.md) · next: [29.7.4.1.2 Assignment and swap \[istream.assign\]](istream.assign.md)

```cpp
explicit basic_istream(basic_streambuf<charT, traits>* sb);
```

**1** *Effects:* Initializes the base class subobject with `basic_ios::init(sb)` ([[basic.ios.cons]](basic.ios.cons.md)).

**2** *Postconditions:* `gcount() == 0`.

```cpp
basic_istream(basic_istream&& rhs);
```

**3** *Effects:* Default constructs the base class, copies the `gcount()` from `rhs`, calls `basic_ios<charT, traits>::move(rhs)` to initialize the base class, and sets the `gcount()` for `rhs` to 0.

```cpp
virtual ~basic_istream();
```

**4** *Remarks:* Does not perform any operations of `rdbuf()`.
