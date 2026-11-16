# 23.5.3.3 Construction and assignment [move.iter.cons]

[Contents](../README.md) · up: [23.5.3 Move iterators and sentinels \[move.iterators\]](move.iterators.md) · prev: [23.5.3.2 Requirements \[move.iter.requirements\]](move.iter.requirements.md) · next: [23.5.3.4 Conversion \[move.iter.op.conv\]](move.iter.op.conv.md)

```cpp
constexpr move_iterator();
```

**1** *Effects:* Constructs a `move_iterator`, value-initializing `current`. Iterator operations applied to the resulting iterator have defined behavior if and only if the corresponding operations are defined on a value-initialized iterator of type `Iterator`.

```cpp
constexpr explicit move_iterator(Iterator i);
```

**2** *Effects:* Constructs a `move_iterator`, initializing `current` with `std::move(i)`.

```cpp
template<class U> constexpr move_iterator(const move_iterator<U>& u);
```

**3** *Mandates:* `U` is convertible to `Iterator`.

**4** *Effects:* Constructs a `move_iterator`, initializing `current` with `u.base()`.

```cpp
template<class U> constexpr move_iterator& operator=(const move_iterator<U>& u);
```

**5** *Mandates:* `U` is convertible to `Iterator`.

**6** *Effects:* Assigns `u.base()` to `current`.
