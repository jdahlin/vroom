# 20.3.3 Alias template `make_integer_sequence` [intseq.make]

[Contents](../README.md) · up: [20.3 Compile-time integer sequences \[intseq\]](intseq.md) · prev: [20.3.2 Class template `integer_sequence` \[intseq.intseq\]](intseq.intseq.md) · next: [20.4 Pairs \[pairs\]](pairs.md)

```cpp
template<class T, T N>
  using make_integer_sequence = integer_sequence<T, see below>;
```

**1** *Mandates:* `N` ≥ 0.

**2** The alias template `make_integer_sequence` denotes a specialization of `integer_sequence` with `N` non-type template arguments. The type `make_integer_sequence<T, N>` is an alias for the type `integer_sequence<T, 0, 1, ..., N-1>`. [*Note 1*: `make_integer_sequence<int, 0>` is an alias for the type `integer_sequence<int>`. — *end note*]
