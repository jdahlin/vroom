# 24.6.3.4 Class `iota_view::sentinel` [range.iota.sentinel]

[Contents](../README.md) · up: [24.6.3 Iota view \[range.iota\]](range.iota.md) · prev: [24.6.3.3 Class `iota_view::iterator` \[range.iota.iterator\]](range.iota.iterator.md) · next: [24.6.4 Istream view \[range.istream\]](range.istream.md)

```cpp
namespace std::ranges {
  template<weakly_incrementable W, semiregular Bound>
    requires weakly-equality-comparable-with<W, Bound>
  struct iota_view<W, Bound>::sentinel {
  private:
    Bound bound_ = Bound();     // exposition only
  public:
    sentinel() = default;
    constexpr explicit sentinel(Bound bound);

    friend constexpr bool operator==(const iterator& x, const sentinel& y);

    friend constexpr iter_difference_t<W> operator-(const iterator& x, const sentinel& y)
      requires sized_sentinel_for<Bound, W>;
    friend constexpr iter_difference_t<W> operator-(const sentinel& x, const iterator& y)
      requires sized_sentinel_for<Bound, W>;
  };
}
```

```cpp
constexpr explicit sentinel(Bound bound);
```

**1** *Effects:* Initializes `bound_` with `bound`.

```cpp
friend constexpr bool operator==(const iterator& x, const sentinel& y);
```

**2** *Effects:* Equivalent to: `return x.value_ == y.bound_;`

```cpp
friend constexpr iter_difference_t<W> operator-(const iterator& x, const sentinel& y)
  requires sized_sentinel_for<Bound, W>;
```

**3** *Effects:* Equivalent to: `return x.value_ - y.bound_;`

```cpp
friend constexpr iter_difference_t<W> operator-(const sentinel& x, const iterator& y)
  requires sized_sentinel_for<Bound, W>;
```

**4** *Effects:* Equivalent to: `return -(y - x);`
