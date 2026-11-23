# 23.5.2.2.1 Operations [front.insert.iter.ops]

[Contents](../../README.md) · up: [23.5.2.2 Class template `front_insert_iterator` \[front.insert.iterator\]](front.insert.iterator.md) · prev: [23.5.2.2 Class template `front_insert_iterator` \[front.insert.iterator\]](front.insert.iterator.md) · next: [23.5.2.2.2 `front_inserter` \[front.inserter\]](front.inserter.md)

```cpp
constexpr explicit front_insert_iterator(Container& x);
```

**1** *Effects:* Initializes `container` with `addressof(x)`.

```cpp
constexpr front_insert_iterator& operator=(const typename Container::value_type& value);
```

**2** *Effects:* As if by: `container->push_front(value);`

**3** *Returns:* `*this`.

```cpp
constexpr front_insert_iterator& operator=(typename Container::value_type&& value);
```

**4** *Effects:* As if by: `container->push_front(std::move(value));`

**5** *Returns:* `*this`.

```cpp
constexpr front_insert_iterator& operator*();
```

**6** *Returns:* `*this`.

```cpp
constexpr front_insert_iterator& operator++();
constexpr front_insert_iterator  operator++(int);
```

**7** *Returns:* `*this`.
