# 29.10.2.6 Specialized algorithms [syncstream.syncbuf.special]

[Contents](../../README.md) · up: [29.10.2 Class template `basic_syncbuf` \[syncstream.syncbuf\]](syncstream.syncbuf.md) · prev: [29.10.2.5 Overridden virtual functions \[syncstream.syncbuf.virtuals\]](syncstream.syncbuf.virtuals.md) · next: [29.10.3 Class template `basic_osyncstream` \[syncstream.osyncstream\]](syncstream.osyncstream.md)

```cpp
template<class charT, class traits, class Allocator>
  void swap(basic_syncbuf<charT, traits, Allocator>& a,
            basic_syncbuf<charT, traits, Allocator>& b) noexcept;
```

**1** *Effects:* Equivalent to `a.swap(b)`.
