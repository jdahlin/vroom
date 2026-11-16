# 21.3.2.6.5 `basic_string::erase` [string.erase]

[Contents](../../README.md) · up: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md) · prev: [21.3.2.6.4 `basic_string::insert` \[string.insert\]](string.insert.md) · next: [21.3.2.6.6 `basic_string::replace` \[string.replace\]](string.replace.md)

```cpp
constexpr basic_string& erase(size_type pos = 0, size_type n = npos);
```

**1** *Throws:* `out_of_range` if `pos` `> size()`.

**2** *Effects:* Determines the effective length `xlen` of the string to be removed as the smaller of `n` and `size() - pos`. Removes the characters in the range `[begin() + pos, begin() + pos + xlen)`.

**3** *Returns:* `*this`.

```cpp
constexpr iterator erase(const_iterator p);
```

**4** *Preconditions:* `p` is a valid dereferenceable iterator on `*this`.

**5** *Throws:* Nothing.

**6** *Effects:* Removes the character referred to by `p`.

**7** *Returns:* An iterator which points to the element immediately following `p` prior to the element being erased. If no such element exists, `end()` is returned.

```cpp
constexpr iterator erase(const_iterator first, const_iterator last);
```

**8** *Preconditions:* `first` and `last` are valid iterators on `*this`. `[first, last)` is a valid range.

**9** *Throws:* Nothing.

**10** *Effects:* Removes the characters in the range `[first, last)`.

**11** *Returns:* An iterator which points to the element pointed to by `last` prior to the other elements being erased. If no such element exists, `end()` is returned.

```cpp
constexpr void pop_back();
```

**12** *Preconditions:* `!empty()`.

**13** *Throws:* Nothing.

**14** *Effects:* Equivalent to `erase(end() - 1)`.
