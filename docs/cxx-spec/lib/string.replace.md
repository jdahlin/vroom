# 21.3.2.6.6 `basic_string::replace` [string.replace]

[Contents](../README.md) · up: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md) · prev: [21.3.2.6.5 `basic_string::erase` \[string.erase\]](string.erase.md) · next: [21.3.2.6.7 `basic_string::copy` \[string.copy\]](string.copy.md)

```cpp
constexpr basic_string& replace(size_type pos1, size_type n1, const basic_string& str);
```

**1** *Effects:* Equivalent to: `return replace(pos1, n1, str.data(), str.size());`

```cpp
constexpr basic_string& replace(size_type pos1, size_type n1, const basic_string& str,
                                size_type pos2, size_type n2 = npos);
```

**2** *Effects:* Equivalent to:

```cpp
return replace(pos1, n1, basic_string_view<charT, traits>(str).substr(pos2, n2));
```

```cpp
template<class T>
  constexpr basic_string& replace(size_type pos1, size_type n1, const T& t);
```

**3** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**4** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return replace(pos1, n1, sv.data(), sv.size());
```

```cpp
template<class T>
  constexpr basic_string& replace(size_type pos1, size_type n1, const T& t,
                                  size_type pos2, size_type n2 = npos);
```

**5** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**6** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return replace(pos1, n1, sv.substr(pos2, n2));
```

```cpp
constexpr basic_string& replace(size_type pos1, size_type n1, const charT* s, size_type n2);
```

**7** *Preconditions:* `[s, s + n2)` is a valid range.

**8** *Throws:*

- `out_of_range` if `pos1 > size()`,
- `length_error` if the length of the resulting string would exceed `max_size()` (see below), or
- any exceptions thrown by `allocator_traits<Allocator>::allocate`.

**9** *Effects:* Determines the effective length `xlen` of the string to be removed as the smaller of `n1` and `size() - pos1`. If `size() - xlen >= max_size() - n2` throws `length_error`. Otherwise, the function replaces the characters in the range `[begin() + pos1, begin() + pos1 + xlen)` with a copy of the range `[s, s + n2)`.

**10** *Returns:* `*this`.

```cpp
constexpr basic_string& replace(size_type pos, size_type n, const charT* s);
```

**11** *Effects:* Equivalent to: `return replace(pos, n, s, traits::length(s));`

```cpp
constexpr basic_string& replace(size_type pos1, size_type n1, size_type n2, charT c);
```

**12** *Throws:*

- `out_of_range` if `pos1 > size()`,
- `length_error` if the length of the resulting string would exceed `max_size()` (see below), or
- any exceptions thrown by `allocator_traits<Allocator>::allocate.`

**13** *Effects:* Determines the effective length `xlen` of the string to be removed as the smaller of `n1` and `size() - pos1`. If `size() - xlen >=` `max_size() - n2` throws `length_error`. Otherwise, the function replaces the characters in the range `[begin() + pos1, begin() + pos1 + xlen)` with `n2` copies of `c`.

**14** *Returns:* `*this`.

```cpp
constexpr basic_string& replace(const_iterator i1, const_iterator i2, const basic_string& str);
```

**15** *Effects:* Equivalent to: `return replace(i1, i2, basic_string_view<charT, traits>(str));`

```cpp
template<class T>
  constexpr basic_string& replace(const_iterator i1, const_iterator i2, const T& t);
```

**16** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**17** *Preconditions:* `[begin(), i1)` and `[i1, i2)` are valid ranges.

**18** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return replace(i1 - begin(), i2 - i1, sv.data(), sv.size());
```

```cpp
constexpr basic_string& replace(const_iterator i1, const_iterator i2, const charT* s, size_type n);
```

**19** *Effects:* Equivalent to: `return replace(i1, i2, basic_string_view<charT, traits>(s, n));`

```cpp
constexpr basic_string& replace(const_iterator i1, const_iterator i2, const charT* s);
```

**20** *Effects:* Equivalent to: `return replace(i1, i2, basic_string_view<charT, traits>(s));`

```cpp
constexpr basic_string& replace(const_iterator i1, const_iterator i2, size_type n, charT c);
```

**21** *Preconditions:* `[begin(), i1)` and `[i1, i2)` are valid ranges.

**22** *Effects:* Equivalent to: `return replace(i1 - begin(), i2 - i1, n, c);`

```cpp
template<class InputIterator>
  constexpr basic_string& replace(const_iterator i1, const_iterator i2,
                                  InputIterator j1, InputIterator j2);
```

**23** *Constraints:* `InputIterator` is a type that qualifies as an input iterator ([[container.requirements.general]](container.requirements.general.md)).

**24** *Effects:* Equivalent to: `return replace(i1, i2, basic_string(j1, j2, get_allocator()));`

```cpp
constexpr basic_string& replace(const_iterator i1, const_iterator i2, initializer_list<charT> il);
```

**25** *Effects:* Equivalent to: `return replace(i1, i2, il.begin(), il.size());`
