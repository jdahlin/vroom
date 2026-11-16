# 22.4.4.3 Element access [map.access]

[Contents](../../README.md) · up: [22.4.4 Class template `map` \[map\]](map.md) · prev: [22.4.4.2 Constructors, copy, and assignment \[map.cons\]](map.cons.md) · next: [22.4.4.4 Modifiers \[map.modifiers\]](map.modifiers.md)

```cpp
mapped_type& operator[](const key_type& x);
```

**1** *Effects:* Equivalent to: `return try_emplace(x).first->second;`

```cpp
mapped_type& operator[](key_type&& x);
```

**2** *Effects:* Equivalent to: `return try_emplace(move(x)).first->second;`

```cpp
mapped_type&       at(const key_type& x);
const mapped_type& at(const key_type& x) const;
```

**3** *Returns:* A reference to the `mapped_type` corresponding to `x` in `*this`.

**4** *Throws:* An exception object of type `out_of_range` if no such element is present.

**5** *Complexity:* Logarithmic.
