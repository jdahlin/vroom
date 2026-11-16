# 23.6.2.2 Operations [ostream.iterator.ops]

[Contents](../README.md) · up: [23.6.2 Class template `ostream_iterator` \[ostream.iterator\]](ostream.iterator.md) · prev: [23.6.2.1 Constructors and destructor \[ostream.iterator.cons.des\]](ostream.iterator.cons.des.md) · next: [23.6.3 Class template `istreambuf_iterator` \[istreambuf.iterator\]](istreambuf.iterator.md)

```cpp
ostream_iterator& operator=(const T& value);
```

**1** *Effects:* As if by:

```cpp
*out_stream << value;
if (delim)
  *out_stream << delim;
return *this;
```

```cpp
ostream_iterator& operator*();
```

**2** *Returns:* `*this`.

```cpp
ostream_iterator& operator++();
ostream_iterator& operator++(int);
```

**3** *Returns:* `*this`.
