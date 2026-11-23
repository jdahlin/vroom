# 29.6.3.2.3 Get area [streambuf.pub.get]

[Contents](../../README.md) · up: [29.6.3.2 Public member functions \[streambuf.members\]](streambuf.members.md) · prev: [29.6.3.2.2 Buffer management and positioning \[streambuf.buffer\]](streambuf.buffer.md) · next: [29.6.3.2.4 Putback \[streambuf.pub.pback\]](streambuf.pub.pback.md)

```cpp
streamsize in_avail();
```

**1** *Returns:* If a read position is available, returns `egptr() - gptr()`. Otherwise returns `showmanyc()` ([[streambuf.virt.get]](streambuf.virt.get.md)).

```cpp
int_type snextc();
```

**2** *Effects:* Calls `sbumpc()`.

**3** *Returns:* If that function returns `traits::eof()`, returns `traits::eof()`. Otherwise, returns `sgetc()`.

```cpp
int_type sbumpc();
```

**4** *Effects:* If the input sequence read position is not available, returns `uflow()`. Otherwise, returns `traits::to_int_type(*gptr())` and increments the next pointer for the input sequence.

```cpp
int_type sgetc();
```

**5** *Returns:* If the input sequence read position is not available, returns `underflow()`. Otherwise, returns `traits::to_int_type(*gptr())`.

```cpp
streamsize sgetn(char_type* s, streamsize n);
```

**6** *Returns:* `xsgetn(s, n)`.
