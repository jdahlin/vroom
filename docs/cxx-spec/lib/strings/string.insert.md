# 21.3.2.6.4 `basic_string::insert` [string.insert]

[Contents](../../README.md) · up: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md) · prev: [21.3.2.6.3 `basic_string::assign` \[string.assign\]](string.assign.md) · next: [21.3.2.6.5 `basic_string::erase` \[string.erase\]](string.erase.md)

```cpp
constexpr basic_string& insert(size_type pos, const basic_string& str);
```

**1** *Effects:* Equivalent to: `return insert(pos, str.data(), str.size());`

```cpp
constexpr basic_string& insert(size_type pos1, const basic_string& str,
                               size_type pos2, size_type n = npos);
```

**2** *Effects:* Equivalent to:

```cpp
return insert(pos1, basic_string_view<charT, traits>(str), pos2, n);
```

```cpp
template<class T>
  constexpr basic_string& insert(size_type pos, const T& t);
```

**3** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**4** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return insert(pos, sv.data(), sv.size());
```

```cpp
template<class T>
  constexpr basic_string& insert(size_type pos1, const T& t,
                                 size_type pos2, size_type n = npos);
```

**5** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**6** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return insert(pos1, sv.substr(pos2, n));
```

```cpp
constexpr basic_string& insert(size_type pos, const charT* s, size_type n);
```

**7** *Preconditions:* `[s, s + n)` is a valid range.

**8** *Throws:*

- `out_of_range` if `pos > size()`,
- `length_error` if `n > max_size() - size()`, or
- any exceptions thrown by `allocator_traits<Allocator>::allocate`.

**9** *Effects:* Inserts a copy of the range `[s, s + n)` immediately before the character at position `pos` if `pos < size()`, or otherwise at the end of the string.

**10** *Returns:* `*this`.

```cpp
constexpr basic_string& insert(size_type pos, const charT* s);
```

**11** *Effects:* Equivalent to: `return insert(pos, s, traits::length(s));`

```cpp
constexpr basic_string& insert(size_type pos, size_type n, charT c);
```

**12** *Effects:* Inserts `n` copies of `c` before the character at position `pos` if `pos < size()`, or otherwise at the end of the string.

**13** *Returns:* `*this`

**14** *Throws:*

- `out_of_range` if `pos > size()`,
- `length_error` if `n > max_size() - size()`, or
- any exceptions thrown by `allocator_traits<Allocator>::allocate`.

```cpp
constexpr iterator insert(const_iterator p, charT c);
```

**15** *Preconditions:* `p` is a valid iterator on `*this`.

**16** *Effects:* Inserts a copy of `c` at the position `p`.

**17** *Returns:* An iterator which refers to the inserted character.

```cpp
constexpr iterator insert(const_iterator p, size_type n, charT c);
```

**18** *Preconditions:* `p` is a valid iterator on `*this`.

**19** *Effects:* Inserts `n` copies of `c` at the position `p`.

**20** *Returns:* An iterator which refers to the first inserted character, or `p` if `n == 0`.

```cpp
template<class InputIterator>
  constexpr iterator insert(const_iterator p, InputIterator first, InputIterator last);
```

**21** *Constraints:* `InputIterator` is a type that qualifies as an input iterator ([[container.requirements.general]](../containers/container.requirements.general.md)).

**22** *Preconditions:* `p` is a valid iterator on `*this`.

**23** *Effects:* Equivalent to `insert(p - begin(), basic_string(first, last, get_allocator()))`.

**24** *Returns:* An iterator which refers to the first inserted character, or `p` if `first == last`.

```cpp
constexpr iterator insert(const_iterator p, initializer_list<charT> il);
```

**25** *Effects:* Equivalent to: `return insert(p, il.begin(), il.end());`
