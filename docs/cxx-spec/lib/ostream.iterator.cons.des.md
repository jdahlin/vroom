# 23.6.2.1 Constructors and destructor [ostream.iterator.cons.des]

[Contents](../README.md) · up: [23.6.2 Class template `ostream_iterator` \[ostream.iterator\]](ostream.iterator.md) · prev: [23.6.2 Class template `ostream_iterator` \[ostream.iterator\]](ostream.iterator.md) · next: [23.6.2.2 Operations \[ostream.iterator.ops\]](ostream.iterator.ops.md)

```cpp
ostream_iterator(ostream_type& s);
```

**1** *Effects:* Initializes `out_stream` with `addressof(s)` and `delim` with `nullptr`.

```cpp
ostream_iterator(ostream_type& s, const charT* delimiter);
```

**2** *Effects:* Initializes `out_stream` with `addressof(s)` and `delim` with `delimiter`.
