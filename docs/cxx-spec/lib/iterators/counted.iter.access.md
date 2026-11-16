# 23.5.6.3 Accessors [counted.iter.access]

[Contents](../../README.md) · up: [23.5.6 Counted iterators \[iterators.counted\]](iterators.counted.md) · prev: [23.5.6.2 Constructors and conversions \[counted.iter.const\]](counted.iter.const.md) · next: [23.5.6.4 Element access \[counted.iter.elem\]](counted.iter.elem.md)

```cpp
constexpr I base() const & requires copy_constructible<I>;
```

**1** *Effects:* Equivalent to: `return current;`

```cpp
constexpr I base() &&;
```

**2** *Returns:* `std::move(current)`.

```cpp
constexpr iter_difference_t<I> count() const noexcept;
```

**3** *Effects:* Equivalent to: `return length;`
