# 23.5.5 Default sentinels [default.sentinels]

[Contents](../README.md) · up: [23.5 Iterator adaptors \[predef.iterators\]](predef.iterators.md) · prev: [23.5.4.7 Customization \[common.iter.cust\]](common.iter.cust.md) · next: [23.5.6 Counted iterators \[iterators.counted\]](iterators.counted.md)

```cpp
namespace std {
  struct default_sentinel_t { };
}
```

**1** Class `default_sentinel_t` is an empty type used to denote the end of a range. It can be used together with iterator types that know the bound of their range (e.g., `counted_iterator` ([[counted.iterator]](counted.iterator.md))).
