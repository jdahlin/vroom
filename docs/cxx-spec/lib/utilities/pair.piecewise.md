# 20.4.5 Piecewise construction [pair.piecewise]

[Contents](../../README.md) · up: [20.4 Pairs \[pairs\]](pairs.md) · prev: [20.4.4 Tuple-like access to pair \[pair.astuple\]](pair.astuple.md) · next: [20.5 Tuples \[tuple\]](tuple.md)

```cpp
struct piecewise_construct_t {
  explicit piecewise_construct_t() = default;
};
inline constexpr piecewise_construct_t piecewise_construct{};
```

**1** The `struct` `piecewise_construct_t` is an empty class type used as a unique type to disambiguate constructor and function overloading. Specifically, `pair` has a constructor with `piecewise_construct_t` as the first argument, immediately followed by two `tuple` ([[tuple]](tuple.md)) arguments used for piecewise construction of the elements of the `pair` object.
