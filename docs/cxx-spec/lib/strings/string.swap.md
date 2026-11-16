# 21.3.2.6.8 `basic_string::swap` [string.swap]

[Contents](../../README.md) · up: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md) · prev: [21.3.2.6.7 `basic_string::copy` \[string.copy\]](string.copy.md) · next: [21.3.2.7 String operations \[string.ops\]](string.ops.md)

```cpp
constexpr void swap(basic_string& s)
  noexcept(allocator_traits<Allocator>::propagate_on_container_swap::value ||
           allocator_traits<Allocator>::is_always_equal::value);
```

**1** *Preconditions:* `allocator_traits<Allocator>::propagate_on_container_swap::value` is `true` or `get_allocator() == s.get_allocator()`.

**2** *Postconditions:* `*this` contains the same sequence of characters that was in `s`, `s` contains the same sequence of characters that was in `*this`.

**3** *Throws:* Nothing.

**4** *Complexity:* Constant time.
