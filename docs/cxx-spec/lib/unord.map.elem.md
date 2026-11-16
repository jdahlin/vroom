# 22.5.4.3 Element access [unord.map.elem]

[Contents](../README.md) · up: [22.5.4 Class template `unordered_map` \[unord.map\]](unord.map.md) · prev: [22.5.4.2 Constructors \[unord.map.cnstr\]](unord.map.cnstr.md) · next: [22.5.4.4 Modifiers \[unord.map.modifiers\]](unord.map.modifiers.md)

```cpp
mapped_type& operator[](const key_type& k);
```

**1** *Effects:* Equivalent to: `return try_emplace(k).first->second;`

```cpp
mapped_type& operator[](key_type&& k);
```

**2** *Effects:* Equivalent to: `return try_emplace(move(k)).first->second;`

```cpp
mapped_type& at(const key_type& k);
const mapped_type& at(const key_type& k) const;
```

**3** *Returns:* A reference to `x.second`, where `x` is the (unique) element whose key is equivalent to `k`.

**4** *Throws:* An exception object of type `out_of_range` if no such element is present.
