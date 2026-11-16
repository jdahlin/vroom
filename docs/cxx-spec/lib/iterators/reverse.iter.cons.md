# 23.5.1.3 Construction and assignment [reverse.iter.cons]

[Contents](../../README.md) · up: [23.5.1 Reverse iterators \[reverse.iterators\]](reverse.iterators.md) · prev: [23.5.1.2 Requirements \[reverse.iter.requirements\]](reverse.iter.requirements.md) · next: [23.5.1.4 Conversion \[reverse.iter.conv\]](reverse.iter.conv.md)

```cpp
constexpr reverse_iterator();
```

**1** *Effects:* Value-initializes `current`. Iterator operations applied to the resulting iterator have defined behavior if and only if the corresponding operations are defined on a value-initialized iterator of type `Iterator`.

```cpp
constexpr explicit reverse_iterator(Iterator x);
```

**2** *Effects:* Initializes `current` with `x`.

```cpp
template<class U> constexpr reverse_iterator(const reverse_iterator<U>& u);
```

**3** *Effects:* Initializes `current` with `u.current`.

```cpp
template<class U>
  constexpr reverse_iterator&
    operator=(const reverse_iterator<U>& u);
```

**4** *Effects:* Assigns `u.base()` to `current`.

**5** *Returns:* `*this`.
