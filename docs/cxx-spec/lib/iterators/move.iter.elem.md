# 23.5.3.5 Element access [move.iter.elem]

[Contents](../../README.md) · up: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · prev: [23.5.3.4 Conversion \[move.iter.op.conv\]](move.iter.op.conv.md) · next: [23.5.3.6 Navigation \[move.iter.nav\]](move.iter.nav.md)

```cpp
constexpr reference operator*() const;
```

**1** *Effects:* Equivalent to: `return ranges::iter_move(current);`

```cpp
constexpr reference operator[](difference_type n) const;
```

**2** *Effects:* Equivalent to: `ranges::iter_move(current + n);`
