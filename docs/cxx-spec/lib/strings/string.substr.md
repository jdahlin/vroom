# 21.3.2.7.3 `basic_string::substr` [string.substr]

[Contents](../../README.md) · up: [21.3.2.7 String operations \[string.ops\]](string.ops.md) · prev: [21.3.2.7.2 Searching \[string.find\]](string.find.md) · next: [21.3.2.7.4 `basic_string::compare` \[string.compare\]](string.compare.md)

```cpp
constexpr basic_string substr(size_type pos = 0, size_type n = npos) const;
```

**1** *Throws:* `out_of_range` if `pos > size()`.

**2** *Effects:* Determines the effective length `rlen` of the string to copy as the smaller of `n` and `size() - pos`.

**3** *Returns:* `basic_string(data()+pos, rlen)`.
