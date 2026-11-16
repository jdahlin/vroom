# 23.6.1.1 Constructors and destructor [istream.iterator.cons]

[Contents](../../README.md) · up: [23.6.1 Class template `istream_iterator` \[istream.iterator\]](istream.iterator.md) · prev: [23.6.1 Class template `istream_iterator` \[istream.iterator\]](istream.iterator.md) · next: [23.6.1.2 Operations \[istream.iterator.ops\]](istream.iterator.ops.md)

```cpp
constexpr istream_iterator();
constexpr istream_iterator(default_sentinel_t);
```

**1** *Effects:* Constructs the end-of-stream iterator, value-initializing `value`.

**2** *Postconditions:* `in_stream == nullptr` is `true`.

**3** *Remarks:* If the initializer `T()` in the declaration `auto x = T();` is a constant initializer ([[expr.const]](../../expr/expr.const.md)), then these constructors are `constexpr` constructors.

```cpp
istream_iterator(istream_type& s);
```

**4** *Effects:* Initializes `in_stream` with `addressof(s)`, value-initializes `value`, and then calls `operator++()`.

```cpp
istream_iterator(const istream_iterator& x) = default;
```

**5** *Postconditions:* `in_stream == x.in_stream` is `true`.

**6** *Remarks:* If `is_trivially_copy_constructible_v<T>` is `true`, then this constructor is trivial.

```cpp
~istream_iterator() = default;
```

**7** *Remarks:* If `is_trivially_destructible_v<T>` is `true`, then this destructor is trivial.
