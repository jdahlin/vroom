# 23.6.3.3 Operations [istreambuf.iterator.ops]

[Contents](../../README.md) · up: [23.6.3 Class template `istreambuf_iterator` \[istreambuf.iterator\]](istreambuf.iterator.md) · prev: [23.6.3.2 Constructors \[istreambuf.iterator.cons\]](istreambuf.iterator.cons.md) · next: [23.6.4 Class template `ostreambuf_iterator` \[ostreambuf.iterator\]](ostreambuf.iterator.md)

```cpp
charT operator*() const;
```

**1** *Returns:* The character obtained via the `streambuf` member `sbuf_->sgetc()`.

```cpp
istreambuf_iterator& operator++();
```

**2** *Effects:* As if by `sbuf_->sbumpc()`.

**3** *Returns:* `*this`.

```cpp
proxy operator++(int);
```

**4** *Returns:* `proxy(sbuf_->sbumpc(), sbuf_)`.

```cpp
bool equal(const istreambuf_iterator& b) const;
```

**5** *Returns:* `true` if and only if both iterators are at end-of-stream, or neither is at end-of-stream, regardless of what `streambuf` object they use.

```cpp
template<class charT, class traits>
  bool operator==(const istreambuf_iterator<charT,traits>& a,
                  const istreambuf_iterator<charT,traits>& b);
```

**6** *Returns:* `a.equal(b)`.

```cpp
friend bool operator==(const istreambuf_iterator& i, default_sentinel_t s);
```

**7** *Returns:* `i.equal(s)`.
