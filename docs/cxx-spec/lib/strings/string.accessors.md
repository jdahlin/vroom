# 21.3.2.7.1 Accessors [string.accessors]

[Contents](../../README.md) · up: [21.3.2.7 String operations \[string.ops\]](string.ops.md) · prev: [21.3.2.7 String operations \[string.ops\]](string.ops.md) · next: [21.3.2.7.2 Searching \[string.find\]](string.find.md)

```cpp
constexpr const charT* c_str() const noexcept;
constexpr const charT* data() const noexcept;
```

**1** *Returns:* A pointer `p` such that `p + i == addressof(operator[](i))` for each `i` in `[0, size()]`.

**2** *Complexity:* Constant time.

**3** *Remarks:* The program shall not modify any of the values stored in the character array; otherwise, the behavior is undefined.

```cpp
constexpr charT* data() noexcept;
```

**4** *Returns:* A pointer `p` such that `p + i == addressof(operator[](i))` for each `i` in `[0, size()]`.

**5** *Complexity:* Constant time.

**6** *Remarks:* The program shall not modify the value stored at `p + size()` to any value other than `charT()`; otherwise, the behavior is undefined.

```cpp
constexpr operator basic_string_view<charT, traits>() const noexcept;
```

**7** *Effects:* Equivalent to: `return basic_string_view<charT, traits>(data(), size());`

```cpp
constexpr allocator_type get_allocator() const noexcept;
```

**8** *Returns:* A copy of the `Allocator` object used to construct the string or, if that allocator has been replaced, a copy of the most recent replacement.
