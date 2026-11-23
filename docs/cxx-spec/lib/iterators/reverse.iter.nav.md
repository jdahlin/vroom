# 23.5.1.6 Navigation [reverse.iter.nav]

[Contents](../../README.md) · up: [23.5.1 Reverse iterators \[reverse.iterators\]](reverse.iterators.md) · prev: [23.5.1.5 Element access \[reverse.iter.elem\]](reverse.iter.elem.md) · next: [23.5.1.7 Comparisons \[reverse.iter.cmp\]](reverse.iter.cmp.md)

```cpp
constexpr reverse_iterator operator+(difference_type n) const;
```

**1** *Returns:* `reverse_iterator(current-n)`.

```cpp
constexpr reverse_iterator operator-(difference_type n) const;
```

**2** *Returns:* `reverse_iterator(current+n)`.

```cpp
constexpr reverse_iterator& operator++();
```

**3** *Effects:* As if by: `--current;`

**4** *Returns:* `*this`.

```cpp
constexpr reverse_iterator operator++(int);
```

**5** *Effects:* As if by:

```cpp
reverse_iterator tmp = *this;
--current;
return tmp;
```

```cpp
constexpr reverse_iterator& operator--();
```

**6** *Effects:* As if by `++current`.

**7** *Returns:* `*this`.

```cpp
constexpr reverse_iterator operator--(int);
```

**8** *Effects:* As if by:

```cpp
reverse_iterator tmp = *this;
++current;
return tmp;
```

```cpp
constexpr reverse_iterator& operator+=(difference_type n);
```

**9** *Effects:* As if by: `current -= n;`

**10** *Returns:* `*this`.

```cpp
constexpr reverse_iterator& operator-=(difference_type n);
```

**11** *Effects:* As if by: `current += n;`

**12** *Returns:* `*this`.
