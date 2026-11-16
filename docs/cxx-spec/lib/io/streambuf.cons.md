# 29.6.3.1 Constructors [streambuf.cons]

[Contents](../../README.md) · up: [29.6.3 Class template `basic_streambuf` \[streambuf\]](streambuf.md) · prev: [29.6.3 Class template `basic_streambuf` \[streambuf\]](streambuf.md) · next: [29.6.3.2 Public member functions \[streambuf.members\]](streambuf.members.md)

```cpp
basic_streambuf();
```

**1** *Effects:* Initializes:[^1]

- all pointer member objects to null pointers,
- the `getloc()` member to a copy the global locale, `locale()`, at the time of construction.

**2** *Remarks:* Once the `getloc()` member is initialized, results of calling locale member functions, and of members of facets so obtained, can safely be cached until the next time the member `imbue` is called.

```cpp
basic_streambuf(const basic_streambuf& rhs);
```

**3** *Postconditions:*

- `eback() == rhs.eback()`
- `gptr() == rhs.gptr()`
- `egptr() == rhs.egptr()`
- `pbase() == rhs.pbase()`
- `pptr() == rhs.pptr()`
- `epptr() == rhs.epptr()`
- `getloc() == rhs.getloc()`

```cpp
~basic_streambuf();
```

**4** *Effects:* None.

[^1]: The default constructor is protected for class `basic_streambuf` to assure that only objects for classes derived from this class may be constructed.
