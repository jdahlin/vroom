# 29.6.3.2.4 Putback [streambuf.pub.pback]

[Contents](../../README.md) · up: [29.6.3.2 Public member functions \[streambuf.members\]](streambuf.members.md) · prev: [29.6.3.2.3 Get area \[streambuf.pub.get\]](streambuf.pub.get.md) · next: [29.6.3.2.5 Put area \[streambuf.pub.put\]](streambuf.pub.put.md)

```cpp
int_type sputbackc(char_type c);
```

**1** *Effects:* If the input sequence putback position is not available, or if `traits::eq(c, gptr()[-1])` is `false`, returns `pbackfail(traits::to_int_type(c))`. Otherwise, decrements the next pointer for the input sequence and returns `traits::to_int_type(*gptr())`.

```cpp
int_type sungetc();
```

**2** *Effects:* If the input sequence putback position is not available, returns `pbackfail()`. Otherwise, decrements the next pointer for the input sequence and returns `traits::to_int_type(*gptr())`.
