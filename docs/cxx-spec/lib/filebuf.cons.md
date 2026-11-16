# 29.9.2.1 Constructors [filebuf.cons]

[Contents](../README.md) · up: [29.9.2 Class template `basic_filebuf` \[filebuf\]](filebuf.md) · prev: [29.9.2 Class template `basic_filebuf` \[filebuf\]](filebuf.md) · next: [29.9.2.2 Assignment and swap \[filebuf.assign\]](filebuf.assign.md)

```cpp
basic_filebuf();
```

**1** *Effects:* Initializes the base class with `basic_streambuf<charT, traits>()` ([[streambuf.cons]](streambuf.cons.md)).

**2** *Postconditions:* `is_open() == false`.

```cpp
basic_filebuf(basic_filebuf&& rhs);
```

**3** *Effects:* It is implementation-defined whether the sequence pointers in `*this` (`eback()`, `gptr()`, `egptr()`, `pbase()`, `pptr()`, `epptr()`) obtain the values which `rhs` had. Whether they do or not, `*this` and `rhs` reference separate buffers (if any at all) after the construction. Additionally `*this` references the file which `rhs` did before the construction, and `rhs` references no file after the construction. The openmode, locale and any other state of `rhs` is also copied.

**4** *Postconditions:* Let `rhs_p` refer to the state of `rhs` just prior to this construction and let `rhs_a` refer to the state of `rhs` just after this construction.

- `is_open() == rhs_p.is_open()`
- `rhs_a.is_open() == false`
- `gptr() - eback() == rhs_p.gptr() - rhs_p.eback()`
- `egptr() - eback() == rhs_p.egptr() - rhs_p.eback()`
- `pptr() - pbase() == rhs_p.pptr() - rhs_p.pbase()`
- `epptr() - pbase() == rhs_p.epptr() - rhs_p.pbase()`
- `if (eback()) eback() != rhs_a.eback()`
- `if (gptr()) gptr() != rhs_a.gptr()`
- `if (egptr()) egptr() != rhs_a.egptr()`
- `if (pbase()) pbase() != rhs_a.pbase()`
- `if (pptr()) pptr() != rhs_a.pptr()`
- `if (epptr()) epptr() != rhs_a.epptr()`

```cpp
virtual ~basic_filebuf();
```

**5** *Effects:* Calls `close()`. If an exception occurs during the destruction of the object, including the call to `close()`, the exception is caught but not rethrown (see [[res.on.exception.handling]](res.on.exception.handling.md)).
