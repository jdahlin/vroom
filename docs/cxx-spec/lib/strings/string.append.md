# 21.3.2.6.2 `basic_string::append` [string.append]

[Contents](../../README.md) · up: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md) · prev: [21.3.2.6.1 `basic_string::operator+=` \[string.op.append\]](string.op.append.md) · next: [21.3.2.6.3 `basic_string::assign` \[string.assign\]](string.assign.md)

```cpp
constexpr basic_string& append(const basic_string& str);
```

**1** *Effects:* Equivalent to: `return append(str.data(), str.size());`

```cpp
constexpr basic_string& append(const basic_string& str, size_type pos, size_type n = npos);
```

**2** *Effects:* Equivalent to:

```cpp
return append(basic_string_view<charT, traits>(str).substr(pos, n));
```

```cpp
template<class T>
  constexpr basic_string& append(const T& t);
```

**3** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**4** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return append(sv.data(), sv.size());
```

```cpp
template<class T>
  constexpr basic_string& append(const T& t, size_type pos, size_type n = npos);
```

**5** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**6** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return append(sv.substr(pos, n));
```

```cpp
constexpr basic_string& append(const charT* s, size_type n);
```

**7** *Preconditions:* `[s, s + n)` is a valid range.

**8** *Effects:* Appends a copy of the range `[s, s + n)` to the string.

**9** *Returns:* `*this`.

```cpp
constexpr basic_string& append(const charT* s);
```

**10** *Effects:* Equivalent to: `return append(s, traits::length(s));`

```cpp
constexpr basic_string& append(size_type n, charT c);
```

**11** *Effects:* Appends `n` copies of `c` to the string.

**12** *Returns:* `*this`.

```cpp
template<class InputIterator>
  constexpr basic_string& append(InputIterator first, InputIterator last);
```

**13** *Constraints:* `InputIterator` is a type that qualifies as an input iterator ([[container.requirements.general]](../containers/container.requirements.general.md)).

**14** *Effects:* Equivalent to: `return append(basic_string(first, last, get_allocator()));`

```cpp
constexpr basic_string& append(initializer_list<charT> il);
```

**15** *Effects:* Equivalent to: `return append(il.begin(), il.size());`

```cpp
constexpr void push_back(charT c);
```

**16** *Effects:* Equivalent to `append(size_type{1}, c)`.
