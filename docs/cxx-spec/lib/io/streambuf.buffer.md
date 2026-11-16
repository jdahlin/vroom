# 29.6.3.2.2 Buffer management and positioning [streambuf.buffer]

[Contents](../../README.md) · up: [29.6.3.2 Public member functions \[streambuf.members\]](streambuf.members.md) · prev: [29.6.3.2.1 Locales \[streambuf.locales\]](streambuf.locales.md) · next: [29.6.3.2.3 Get area \[streambuf.pub.get\]](streambuf.pub.get.md)

```cpp
basic_streambuf* pubsetbuf(char_type* s, streamsize n);
```

**1** *Returns:* `setbuf(s, n)`.

```cpp
pos_type pubseekoff(off_type off, ios_base::seekdir way,
                    ios_base::openmode which
                      = ios_base::in | ios_base::out);
```

**2** *Returns:* `seekoff(off, way, which)`.

```cpp
pos_type pubseekpos(pos_type sp,
                    ios_base::openmode which
                      = ios_base::in | ios_base::out);
```

**3** *Returns:* `seekpos(sp, which)`.

```cpp
int pubsync();
```

**4** *Returns:* `sync()`.
