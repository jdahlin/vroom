# 21.3.2.3 Iterator support [string.iterators]

[Contents](../../README.md) · up: [21.3.2 Class template `basic_string` \[basic.string\]](basic.string.md) · prev: [21.3.2.2 Constructors and assignment operators \[string.cons\]](string.cons.md) · next: [21.3.2.4 Capacity \[string.capacity\]](string.capacity.md)

```cpp
constexpr iterator       begin() noexcept;
constexpr const_iterator begin() const noexcept;
constexpr const_iterator cbegin() const noexcept;
```

**1** *Returns:* An iterator referring to the first character in the string.

```cpp
constexpr iterator       end() noexcept;
constexpr const_iterator end() const noexcept;
constexpr const_iterator cend() const noexcept;
```

**2** *Returns:* An iterator which is the past-the-end value.

```cpp
constexpr reverse_iterator       rbegin() noexcept;
constexpr const_reverse_iterator rbegin() const noexcept;
constexpr const_reverse_iterator crbegin() const noexcept;
```

**3** *Returns:* An iterator which is semantically equivalent to `reverse_iterator(end())`.

```cpp
constexpr reverse_iterator       rend() noexcept;
constexpr const_reverse_iterator rend() const noexcept;
constexpr const_reverse_iterator crend() const noexcept;
```

**4** *Returns:* An iterator which is semantically equivalent to `reverse_iterator(begin())`.
