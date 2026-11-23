# 21.3.2.6.3 `basic_string::assign` [string.assign]

[Contents](../../README.md) · up: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md) · prev: [21.3.2.6.2 `basic_string::append` \[string.append\]](string.append.md) · next: [21.3.2.6.4 `basic_string::insert` \[string.insert\]](string.insert.md)

```cpp
constexpr basic_string& assign(const basic_string& str);
```

**1** *Effects:* Equivalent to: `return *this = str;`

```cpp
constexpr basic_string& assign(basic_string&& str)
  noexcept(allocator_traits<Allocator>::propagate_on_container_move_assignment::value ||
           allocator_traits<Allocator>::is_always_equal::value);
```

**2** *Effects:* Equivalent to: `return *this = std::move(str);`

```cpp
constexpr basic_string& assign(const basic_string& str, size_type pos, size_type n = npos);
```

**3** *Effects:* Equivalent to:

```cpp
return assign(basic_string_view<charT, traits>(str).substr(pos, n));
```

```cpp
template<class T>
  constexpr basic_string& assign(const T& t);
```

**4** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**5** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return assign(sv.data(), sv.size());
```

```cpp
template<class T>
  constexpr basic_string& assign(const T& t, size_type pos, size_type n = npos);
```

**6** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**7** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return assign(sv.substr(pos, n));
```

```cpp
constexpr basic_string& assign(const charT* s, size_type n);
```

**8** *Preconditions:* `[s, s + n)` is a valid range.

**9** *Effects:* Replaces the string controlled by `*this` with a copy of the range `[s, s + n)`.

**10** *Returns:* `*this`.

```cpp
constexpr basic_string& assign(const charT* s);
```

**11** *Effects:* Equivalent to: `return assign(s, traits::length(s));`

```cpp
constexpr basic_string& assign(initializer_list<charT> il);
```

**12** *Effects:* Equivalent to: `return assign(il.begin(), il.size());`

```cpp
constexpr basic_string& assign(size_type n, charT c);
```

**13** *Effects:* Equivalent to:

```cpp
clear();
resize(n, c);
return *this;
```

```cpp
template<class InputIterator>
  constexpr basic_string& assign(InputIterator first, InputIterator last);
```

**14** *Constraints:* `InputIterator` is a type that qualifies as an input iterator ([[container.requirements.general]](../containers/container.requirements.general.md)).

**15** *Effects:* Equivalent to: `return assign(basic_string(first, last, get_allocator()));`
