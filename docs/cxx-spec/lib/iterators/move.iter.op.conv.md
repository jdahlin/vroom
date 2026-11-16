# 23.5.3.4 Conversion [move.iter.op.conv]

[Contents](../../README.md) · up: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · prev: [23.5.3.3 Construction and assignment \[move.iter.cons\]](move.iter.cons.md) · next: [23.5.3.5 Element access \[move.iter.elem\]](move.iter.elem.md)

```cpp
constexpr Iterator base() const &;
```

**1** *Constraints:* `Iterator` satisfies `copy_constructible`.

**2** *Preconditions:* `Iterator` models `copy_constructible`.

**3** *Returns:* `current`.

```cpp
constexpr Iterator base() &&;
```

**4** *Returns:* `std::move(current)`.
