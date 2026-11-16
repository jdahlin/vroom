# 21.3.2.5 Element access [string.access]

[Contents](../README.md) · up: [21.3.2 Class template `basic_string` \[basic.string\]](basic.string.md) · prev: [21.3.2.4 Capacity \[string.capacity\]](string.capacity.md) · next: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md)

```cpp
constexpr const_reference operator[](size_type pos) const;
constexpr reference       operator[](size_type pos);
```

**1** *Preconditions:* `pos <= size()`.

**2** *Returns:* `*(begin() + pos)` if `pos < size()`. Otherwise, returns a reference to an object of type `charT` with value `charT()`, where modifying the object to any value other than `charT()` leads to undefined behavior.

**3** *Throws:* Nothing.

**4** *Complexity:* Constant time.

```cpp
constexpr const_reference at(size_type pos) const;
constexpr reference       at(size_type pos);
```

**5** *Throws:* `out_of_range` if `pos >= size()`.

**6** *Returns:* `operator[](pos)`.

```cpp
constexpr const charT& front() const;
constexpr charT& front();
```

**7** *Preconditions:* `!empty()`.

**8** *Effects:* Equivalent to: `return operator[](0);`

```cpp
constexpr const charT& back() const;
constexpr charT& back();
```

**9** *Preconditions:* `!empty()`.

**10** *Effects:* Equivalent to: `return operator[](size() - 1);`
