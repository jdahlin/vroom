# 22.6.6.4 Operators [stack.ops]

[Contents](../README.md) · up: [22.6.6 Class template `stack` \[stack\]](stack.md) · prev: [22.6.6.3 Constructors with allocators \[stack.cons.alloc\]](stack.cons.alloc.md) · next: [22.6.6.5 Specialized algorithms \[stack.special\]](stack.special.md)

```cpp
template<class T, class Container>
  bool operator==(const stack<T, Container>& x, const stack<T, Container>& y);
```

**1** *Returns:* `x.c == y.c`.

```cpp
template<class T, class Container>
  bool operator!=(const stack<T, Container>& x, const stack<T, Container>& y);
```

**2** *Returns:* `x.c != y.c`.

```cpp
template<class T, class Container>
  bool operator< (const stack<T, Container>& x, const stack<T, Container>& y);
```

**3** *Returns:* `x.c < y.c`.

```cpp
template<class T, class Container>
  bool operator> (const stack<T, Container>& x, const stack<T, Container>& y);
```

**4** *Returns:* `x.c > y.c`.

```cpp
template<class T, class Container>
  bool operator<=(const stack<T, Container>& x, const stack<T, Container>& y);
```

**5** *Returns:* `x.c <= y.c`.

```cpp
template<class T, class Container>
    bool operator>=(const stack<T, Container>& x, const stack<T, Container>& y);
```

**6** *Returns:* `x.c >= y.c`.

```cpp
template<class T, three_way_comparable Container>
  compare_three_way_result_t<Container>
    operator<=>(const stack<T, Container>& x, const stack<T, Container>& y);
```

**7** *Returns:* `x.c <=> y.c`.
