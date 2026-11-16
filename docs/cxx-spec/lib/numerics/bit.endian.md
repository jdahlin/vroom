# 26.5.7 Endian [bit.endian]

[Contents](../../README.md) · up: [26.5 Bit manipulation \[bit\]](bit.md) · prev: [26.5.6 Counting \[bit.count\]](bit.count.md) · next: [26.6 Random number generation \[rand\]](rand.md)

**1** Two common methods of byte ordering in multibyte scalar types are big-endian and little-endian in the execution environment. Big-endian is a format for storage of binary data in which the most significant byte is placed first, with the rest in descending order. Little-endian is a format for storage of binary data in which the least significant byte is placed first, with the rest in ascending order. This subclause describes the endianness of the scalar types of the execution environment.

```cpp
enum class endian {
  little = see below,
  big    = see below,
  native = see below
};
```

**2** If all scalar types have size 1 byte, then all of `endian::little`, `endian::big`, and `endian::native` have the same value. Otherwise, `endian::little` is not equal to `endian::big`. If all scalar types are big-endian, `endian::native` is equal to `endian::big`. If all scalar types are little-endian, `endian::native` is equal to `endian::little`. Otherwise, `endian::native` is not equal to either `endian::big` or `endian::little`.
