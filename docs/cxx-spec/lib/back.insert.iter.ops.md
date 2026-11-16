# 23.5.2.1.1 Operations [back.insert.iter.ops]

[Contents](../README.md) · up: [23.5.2.1 Class template `back_insert_iterator` \[back.insert.iterator\]](back.insert.iterator.md) · prev: [23.5.2.1 Class template `back_insert_iterator` \[back.insert.iterator\]](back.insert.iterator.md) · next: [23.5.2.1.2 `back_inserter` \[back.inserter\]](back.inserter.md)

```cpp
constexpr explicit back_insert_iterator(Container& x);
```

**1** *Effects:* Initializes `container` with `addressof(x)`.

```cpp
constexpr back_insert_iterator& operator=(const typename Container::value_type& value);
```

**2** *Effects:* As if by: `container->push_back(value);`

**3** *Returns:* `*this`.

```cpp
constexpr back_insert_iterator& operator=(typename Container::value_type&& value);
```

**4** *Effects:* As if by: `container->push_back(std::move(value));`

**5** *Returns:* `*this`.

```cpp
constexpr back_insert_iterator& operator*();
```

**6** *Returns:* `*this`.

```cpp
constexpr back_insert_iterator& operator++();
constexpr back_insert_iterator  operator++(int);
```

**7** *Returns:* `*this`.
