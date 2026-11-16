# 29.6.3.2.5 Put area [streambuf.pub.put]

[Contents](../../README.md) · up: [29.6.3.2 Public member functions \[streambuf.members\]](streambuf.members.md) · prev: [29.6.3.2.4 Putback \[streambuf.pub.pback\]](streambuf.pub.pback.md) · next: [29.6.3.3 Protected member functions \[streambuf.protected\]](streambuf.protected.md)

```cpp
int_type sputc(char_type c);
```

**1** *Effects:* If the output sequence write position is not available, returns `overflow(traits::to_int_type(c))`. Otherwise, stores `c` at the next pointer for the output sequence, increments the pointer, and returns `traits::to_int_type(c)`.

```cpp
streamsize sputn(const char_type* s, streamsize n);
```

**2** *Returns:* `xsputn(s, n)`.
