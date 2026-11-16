# 24.5.3.2 Accessors [range.subrange.access]

[Contents](../../README.md) · up: [24.5.3 Sub-ranges \[range.subrange\]](range.subrange.md) · prev: [24.5.3.1 Constructors and conversions \[range.subrange.ctor\]](range.subrange.ctor.md) · next: [24.5.4 Dangling iterator handling \[range.dangling\]](range.dangling.md)

```cpp
constexpr I begin() const requires copyable<I>;
```

**1** *Effects:* Equivalent to: `return begin_;`

```cpp
[[nodiscard]] constexpr I begin() requires (!copyable<I>);
```

**2** *Effects:* Equivalent to: `return std::move(begin_);`

```cpp
constexpr S end() const;
```

**3** *Effects:* Equivalent to: `return end_;`

```cpp
constexpr bool empty() const;
```

**4** *Effects:* Equivalent to: `return begin_ == end_;`

```cpp
constexpr make-unsigned-like-t<iter_difference_t<I>> size() const
  requires (K == subrange_kind::sized);
```

**5** *Effects:*

- If `StoreSize` is `true`, equivalent to: `return size_;`
- Otherwise, equivalent to: `return to-unsigned-like(end_ - begin_);`

```cpp
[[nodiscard]] constexpr subrange next(iter_difference_t<I> n = 1) const &
  requires forward_iterator<I>;
```

**6** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
tmp.advance(n);
return tmp;
```

```cpp
[[nodiscard]] constexpr subrange next(iter_difference_t<I> n = 1) &&;
```

**7** *Effects:* Equivalent to:

```cpp
advance(n);
return std::move(*this);
```

```cpp
[[nodiscard]] constexpr subrange prev(iter_difference_t<I> n = 1) const
  requires bidirectional_iterator<I>;
```

**8** *Effects:* Equivalent to:

```cpp
auto tmp = *this;
tmp.advance(-n);
return tmp;
```

```cpp
constexpr subrange& advance(iter_difference_t<I> n);
```

**9** *Effects:* Equivalent to:

- If `StoreSize` is `true`,

  ```cpp
  auto d = n - ranges::advance(begin_, n, end_);
  if (d >= 0)
    size_ -= to-unsigned-like(d);
  else
    size_ += to-unsigned-like(-d);
  return *this;
  ```

- Otherwise,

  ```cpp
  ranges::advance(begin_, n, end_);
  return *this;
  ```

```cpp
template<size_t N, class I, class S, subrange_kind K>
  requires (N < 2)
  constexpr auto get(const subrange<I, S, K>& r);
template<size_t N, class I, class S, subrange_kind K>
  requires (N < 2)
  constexpr auto get(subrange<I, S, K>&& r);
```

**10** *Effects:* Equivalent to:

```cpp
if constexpr (N == 0)
  return r.begin();
else
  return r.end();
```
