# 29.7.5.1.1 Constructors [ostream.cons]

[Contents](../../README.md) · up: [29.7.5.1 Class template `basic_ostream` \[ostream\]](ostream.md) · prev: [29.7.5.1 Class template `basic_ostream` \[ostream\]](ostream.md) · next: [29.7.5.1.2 Assignment and swap \[ostream.assign\]](ostream.assign.md)

```cpp
explicit basic_ostream(basic_streambuf<charT, traits>* sb);
```

**1** *Effects:* Initializes the base class subobject with `basic_ios<charT, traits>::init(sb)` ([[basic.ios.cons]](basic.ios.cons.md)).

**2** *Postconditions:* `rdbuf() == sb`.

```cpp
basic_ostream(basic_ostream&& rhs);
```

**3** *Effects:* Move constructs from the rvalue `rhs`. This is accomplished by default constructing the base class and calling `basic_ios<charT, traits>::move(rhs)` to initialize the base class.

```cpp
virtual ~basic_ostream();
```

**4** *Remarks:* Does not perform any operations on `rdbuf()`.
