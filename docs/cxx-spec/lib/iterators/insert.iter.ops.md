# 23.5.2.3.1 Operations [insert.iter.ops]

[Contents](../../README.md) · up: [23.5.2.3 Class template `insert_iterator` \[insert.iterator\]](insert.iterator.md) · prev: [23.5.2.3 Class template `insert_iterator` \[insert.iterator\]](insert.iterator.md) · next: [23.5.2.3.2 `inserter` \[inserter\]](inserter.md)

```cpp
constexpr insert_iterator(Container& x, ranges::iterator_t<Container> i);
```

**1** *Effects:* Initializes `container` with `addressof(x)` and `iter` with `i`.

```cpp
constexpr insert_iterator& operator=(const typename Container::value_type& value);
```

**2** *Effects:* As if by:

```cpp
iter = container->insert(iter, value);
++iter;
```

**3** *Returns:* `*this`.

```cpp
constexpr insert_iterator& operator=(typename Container::value_type&& value);
```

**4** *Effects:* As if by:

```cpp
iter = container->insert(iter, std::move(value));
++iter;
```

**5** *Returns:* `*this`.

```cpp
constexpr insert_iterator& operator*();
```

**6** *Returns:* `*this`.

```cpp
constexpr insert_iterator& operator++();
constexpr insert_iterator& operator++(int);
```

**7** *Returns:* `*this`.
