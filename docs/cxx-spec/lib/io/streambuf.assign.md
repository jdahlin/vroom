# 29.6.3.3.1 Assignment [streambuf.assign]

[Contents](../../README.md) · up: [29.6.3.3 Protected member functions \[streambuf.protected\]](streambuf.protected.md) · prev: [29.6.3.3 Protected member functions \[streambuf.protected\]](streambuf.protected.md) · next: [29.6.3.3.2 Get area access \[streambuf.get.area\]](streambuf.get.area.md)

```cpp
basic_streambuf& operator=(const basic_streambuf& rhs);
```

**1** *Postconditions:*

- `eback() == rhs.eback()`
- `gptr() == rhs.gptr()`
- `egptr() == rhs.egptr()`
- `pbase() == rhs.pbase()`
- `pptr() == rhs.pptr()`
- `epptr() == rhs.epptr()`
- `getloc() == rhs.getloc()`

**2** *Returns:* `*this`.

```cpp
void swap(basic_streambuf& rhs);
```

**3** *Effects:* Swaps the data members of `rhs` and `*this`.
