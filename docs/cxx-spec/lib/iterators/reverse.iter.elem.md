# 23.5.1.5 Element access [reverse.iter.elem]

[Contents](../../README.md) · up: [23.5.1 Reverse iterators \[reverse.iterators\]](reverse.iterators.md) · prev: [23.5.1.4 Conversion \[reverse.iter.conv\]](reverse.iter.conv.md) · next: [23.5.1.6 Navigation \[reverse.iter.nav\]](reverse.iter.nav.md)

```cpp
constexpr reference operator*() const;
```

**1** *Effects:* As if by:

```cpp
Iterator tmp = current;
return *--tmp;
```

```cpp
constexpr pointer operator->() const
  requires (is_pointer_v<Iterator> ||
            requires (const Iterator i) { i.operator->(); });
```

**2** *Effects:*

- If `Iterator` is a pointer type, equivalent to: `return prev(current);`
- Otherwise, equivalent to: `return prev(current).operator->();`

```cpp
constexpr unspecified operator[](difference_type n) const;
```

**3** *Returns:* `current[-n-1]`.
