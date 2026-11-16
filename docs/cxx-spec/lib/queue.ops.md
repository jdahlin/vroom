# 22.6.4.4 Operators [queue.ops]

[Contents](../README.md) · up: [22.6.4 Class template `queue` \[queue\]](queue.md) · prev: [22.6.4.3 Constructors with allocators \[queue.cons.alloc\]](queue.cons.alloc.md) · next: [22.6.4.5 Specialized algorithms \[queue.special\]](queue.special.md)

```cpp
template<class T, class Container>
  bool operator==(const queue<T, Container>& x, const queue<T, Container>& y);
```

**1** *Returns:* `x.c == y.c`.

```cpp
template<class T, class Container>
  bool operator!=(const queue<T, Container>& x,  const queue<T, Container>& y);
```

**2** *Returns:* `x.c != y.c`.

```cpp
template<class T, class Container>
  bool operator< (const queue<T, Container>& x, const queue<T, Container>& y);
```

**3** *Returns:* `x.c < y.c`.

```cpp
template<class T, class Container>
  bool operator> (const queue<T, Container>& x, const queue<T, Container>& y);
```

**4** *Returns:* `x.c > y.c`.

```cpp
template<class T, class Container>
  bool operator<=(const queue<T, Container>& x, const queue<T, Container>& y);
```

**5** *Returns:* `x.c <= y.c`.

```cpp
template<class T, class Container>
    bool operator>=(const queue<T, Container>& x,
                    const queue<T, Container>& y);
```

**6** *Returns:* `x.c >= y.c`.

```cpp
template<class T, three_way_comparable Container>
  compare_three_way_result_t<Container>
    operator<=>(const queue<T, Container>& x, const queue<T, Container>& y);
```

**7** *Returns:* `x.c <=> y.c`.
