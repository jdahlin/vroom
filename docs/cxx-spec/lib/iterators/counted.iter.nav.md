# 23.5.6.5 Navigation [counted.iter.nav]

[Contents](../../README.md) · up: [23.5.6 Counted iterators \[iterators.counted\]](iterators.counted.md) · prev: [23.5.6.4 Element access \[counted.iter.elem\]](counted.iter.elem.md) · next: [23.5.6.6 Comparisons \[counted.iter.cmp\]](counted.iter.cmp.md)

```cpp
constexpr counted_iterator& operator++();
```

**1** *Preconditions:* `length > 0`.

**2** *Effects:* Equivalent to:

```cpp
++current;
--length;
return *this;
```

```cpp
decltype(auto) operator++(int);
```

**3** *Preconditions:* `length > 0`.

**4** *Effects:* Equivalent to:

```cpp
--length;
try { return current++; }
catch(...) { ++length; throw; }
```

```cpp
constexpr counted_iterator operator++(int)
  requires forward_iterator<I>;
```

**5** *Effects:* Equivalent to:

```cpp
counted_iterator tmp = *this;
++*this;
return tmp;
```

```cpp
constexpr counted_iterator& operator--()
  requires bidirectional_iterator<I>;
```

**6** *Effects:* Equivalent to:

```cpp
--current;
++length;
return *this;
```

```cpp
constexpr counted_iterator operator--(int)
  requires bidirectional_iterator<I>;
```

**7** *Effects:* Equivalent to:

```cpp
counted_iterator tmp = *this;
--*this;
return tmp;
```

```cpp
constexpr counted_iterator operator+(iter_difference_t<I> n) const
  requires random_access_iterator<I>;
```

**8** *Effects:* Equivalent to: `return counted_iterator(current + n, length - n);`

```cpp
friend constexpr counted_iterator operator+(
  iter_difference_t<I> n, const counted_iterator& x)
    requires random_access_iterator<I>;
```

**9** *Effects:* Equivalent to: `return x + n;`

```cpp
constexpr counted_iterator& operator+=(iter_difference_t<I> n)
  requires random_access_iterator<I>;
```

**10** *Preconditions:* `n <= length`.

**11** *Effects:* Equivalent to:

```cpp
current += n;
length -= n;
return *this;
```

```cpp
constexpr counted_iterator operator-(iter_difference_t<I> n) const
  requires random_access_iterator<I>;
```

**12** *Effects:* Equivalent to: `return counted_iterator(current - n, length + n);`

```cpp
template<common_with<I> I2>
  friend constexpr iter_difference_t<I2> operator-(
    const counted_iterator& x, const counted_iterator<I2>& y);
```

**13** *Preconditions:* `x` and `y` refer to elements of the same sequence ([[counted.iterator]](counted.iterator.md)).

**14** *Effects:* Equivalent to: `return y.length - x.length;`

```cpp
friend constexpr iter_difference_t<I> operator-(
  const counted_iterator& x, default_sentinel_t);
```

**15** *Effects:* Equivalent to: `return -x.length;`

```cpp
friend constexpr iter_difference_t<I> operator-(
  default_sentinel_t, const counted_iterator& y);
```

**16** *Effects:* Equivalent to: `return y.length;`

```cpp
constexpr counted_iterator& operator-=(iter_difference_t<I> n)
  requires random_access_iterator<I>;
```

**17** *Preconditions:* `-n <= length`.

**18** *Effects:* Equivalent to:

```cpp
current -= n;
length += n;
return *this;
```
