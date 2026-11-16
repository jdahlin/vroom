# 23.6.4.1 Constructors [ostreambuf.iter.cons]

[Contents](../README.md) · up: [23.6.4 Class template `ostreambuf_iterator` \[ostreambuf.iterator\]](ostreambuf.iterator.md) · prev: [23.6.4 Class template `ostreambuf_iterator` \[ostreambuf.iterator\]](ostreambuf.iterator.md) · next: [23.6.4.2 Operations \[ostreambuf.iter.ops\]](ostreambuf.iter.ops.md)

```cpp
ostreambuf_iterator(ostream_type& s) noexcept;
```

**1** *Preconditions:* `s.rdbuf()` is not a null pointer.

**2** *Effects:* Initializes `sbuf_` with `s.rdbuf()`.

```cpp
ostreambuf_iterator(streambuf_type* s) noexcept;
```

**3** *Preconditions:* `s` is not a null pointer.

**4** *Effects:* Initializes `sbuf_` with `s`.
