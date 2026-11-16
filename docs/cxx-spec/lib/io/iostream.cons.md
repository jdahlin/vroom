# 29.7.4.6.1 Constructors [iostream.cons]

[Contents](../../README.md) · up: [29.7.4.6 Class template `basic_iostream` \[iostreamclass\]](iostreamclass.md) · prev: [29.7.4.6 Class template `basic_iostream` \[iostreamclass\]](iostreamclass.md) · next: [29.7.4.6.2 Destructor \[iostream.dest\]](iostream.dest.md)

```cpp
explicit basic_iostream(basic_streambuf<charT, traits>* sb);
```

**1** *Effects:* Initializes the base class subobjects with `basic_istream<charT, traits>(sb)` ([[istream]](istream.md)) and `basic_ostream<charT, traits>(sb)` ([[ostream]](ostream.md)).

**2** *Postconditions:* `rdbuf() == sb` and `gcount() == 0`.

```cpp
basic_iostream(basic_iostream&& rhs);
```

**3** *Effects:* Move constructs from the rvalue `rhs` by constructing the `basic_istream` base class with `move(rhs)`.
