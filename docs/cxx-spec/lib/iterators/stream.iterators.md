# 23.6 Stream iterators [stream.iterators]

[Contents](../../README.md) · up: [23 Iterators library \[iterators\]](iterators.md) · prev: [23.5.7.1 Class `unreachable_sentinel_t` \[unreachable.sentinel\]](unreachable.sentinel.md) · next: [23.6.1 Class template `istream_iterator` \[istream.iterator\]](istream.iterator.md)

**1** To make it possible for algorithmic templates to work directly with input/output streams, appropriate iterator-like class templates are provided.

[*Example 1*:

```cpp
partial_sum(istream_iterator<double, char>(cin),
  istream_iterator<double, char>(),
  ostream_iterator<double, char>(cout, "\n"));
```

reads a file containing floating-point numbers from `cin`, and prints the partial sums onto `cout`. — *end example*]

## Subclauses

- [23.6.1 Class template `istream_iterator` \[istream.iterator\]](istream.iterator.md)
  - [23.6.1.1 Constructors and destructor \[istream.iterator.cons\]](istream.iterator.cons.md)
  - [23.6.1.2 Operations \[istream.iterator.ops\]](istream.iterator.ops.md)
- [23.6.2 Class template `ostream_iterator` \[ostream.iterator\]](ostream.iterator.md)
  - [23.6.2.1 Constructors and destructor \[ostream.iterator.cons.des\]](ostream.iterator.cons.des.md)
  - [23.6.2.2 Operations \[ostream.iterator.ops\]](ostream.iterator.ops.md)
- [23.6.3 Class template `istreambuf_iterator` \[istreambuf.iterator\]](istreambuf.iterator.md)
  - [23.6.3.1 Class `istreambuf_iterator::proxy` \[istreambuf.iterator.proxy\]](istreambuf.iterator.proxy.md)
  - [23.6.3.2 Constructors \[istreambuf.iterator.cons\]](istreambuf.iterator.cons.md)
  - [23.6.3.3 Operations \[istreambuf.iterator.ops\]](istreambuf.iterator.ops.md)
- [23.6.4 Class template `ostreambuf_iterator` \[ostreambuf.iterator\]](ostreambuf.iterator.md)
  - [23.6.4.1 Constructors \[ostreambuf.iter.cons\]](ostreambuf.iter.cons.md)
  - [23.6.4.2 Operations \[ostreambuf.iter.ops\]](ostreambuf.iter.ops.md)
