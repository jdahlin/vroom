# 23.5.3.6 Navigation [move.iter.nav]

[Contents](../../README.md) · up: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · prev: [23.5.3.5 Element access \[move.iter.elem\]](move.iter.elem.md) · next: [23.5.3.7 Comparisons \[move.iter.op.comp\]](move.iter.op.comp.md)

```cpp
constexpr move_iterator& operator++();
```

**1** *Effects:* As if by `++current`.

**2** *Returns:* `*this`.

```cpp
constexpr auto operator++(int);
```

**3** *Effects:* If `Iterator` models `forward_iterator`, equivalent to:

```cpp
move_iterator tmp = *this;
++current;
return tmp;
```

Otherwise, equivalent to `++current`.

```cpp
constexpr move_iterator& operator--();
```

**4** *Effects:* As if by `--current`.

**5** *Returns:* `*this`.

```cpp
constexpr move_iterator operator--(int);
```

**6** *Effects:* As if by:

```cpp
move_iterator tmp = *this;
--current;
return tmp;
```

```cpp
constexpr move_iterator operator+(difference_type n) const;
```

**7** *Returns:* `move_iterator(current + n)`.

```cpp
constexpr move_iterator& operator+=(difference_type n);
```

**8** *Effects:* As if by: `current += n;`

**9** *Returns:* `*this`.

```cpp
constexpr move_iterator operator-(difference_type n) const;
```

**10** *Returns:* `move_iterator(current - n)`.

```cpp
constexpr move_iterator& operator-=(difference_type n);
```

**11** *Effects:* As if by: `current -= n;`

**12** *Returns:* `*this`.
