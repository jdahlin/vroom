# 23.6.3.2 Constructors [istreambuf.iterator.cons]

[Contents](../README.md) · up: [23.6.3 Class template `istreambuf_iterator` \[istreambuf.iterator\]](istreambuf.iterator.md) · prev: [23.6.3.1 Class `istreambuf_iterator::proxy` \[istreambuf.iterator.proxy\]](istreambuf.iterator.proxy.md) · next: [23.6.3.3 Operations \[istreambuf.iterator.ops\]](istreambuf.iterator.ops.md)

**1** For each `istreambuf_iterator` constructor in this subclause, an end-of-stream iterator is constructed if and only if the exposition-only member `sbuf_` is initialized with a null pointer value.

```cpp
constexpr istreambuf_iterator() noexcept;
constexpr istreambuf_iterator(default_sentinel_t) noexcept;
```

**2** *Effects:* Initializes `sbuf_` with `nullptr`.

```cpp
istreambuf_iterator(istream_type& s) noexcept;
```

**3** *Effects:* Initializes `sbuf_` with `s.rdbuf()`.

```cpp
istreambuf_iterator(streambuf_type* s) noexcept;
```

**4** *Effects:* Initializes `sbuf_` with `s`.

```cpp
istreambuf_iterator(const proxy& p) noexcept;
```

**5** *Effects:* Initializes `sbuf_` with `p.sbuf_`.
