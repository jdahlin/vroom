# 23.6.4.2 Operations [ostreambuf.iter.ops]

[Contents](../../README.md) · up: [23.6.4 Class template `ostreambuf_iterator` \[ostreambuf.iterator\]](ostreambuf.iterator.md) · prev: [23.6.4.1 Constructors \[ostreambuf.iter.cons\]](ostreambuf.iter.cons.md) · next: [23.7 Range access \[iterator.range\]](iterator.range.md)

```cpp
ostreambuf_iterator& operator=(charT c);
```

**1** *Effects:* If `failed()` yields `false`, calls `sbuf_->sputc(c)`; otherwise has no effect.

**2** *Returns:* `*this`.

```cpp
ostreambuf_iterator& operator*();
```

**3** *Returns:* `*this`.

```cpp
ostreambuf_iterator& operator++();
ostreambuf_iterator& operator++(int);
```

**4** *Returns:* `*this`.

```cpp
bool failed() const noexcept;
```

**5** *Returns:* `true` if in any prior use of member `operator=`, the call to `sbuf_->sputc()` returned `traits::eof()`; or `false` otherwise.
