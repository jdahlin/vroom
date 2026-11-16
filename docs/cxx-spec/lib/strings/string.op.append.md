# 21.3.2.6.1 `basic_string::operator+=` [string.op.append]

[Contents](../../README.md) · up: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md) · prev: [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md) · next: [21.3.2.6.2 `basic_string::append` \[string.append\]](string.append.md)

```cpp
constexpr basic_string& operator+=(const basic_string& str);
```

**1** *Effects:* Equivalent to: `return append(str);`

```cpp
template<class T>
  constexpr basic_string& operator+=(const T& t);
```

**2** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**3** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> sv = t;
return append(sv);
```

```cpp
constexpr basic_string& operator+=(const charT* s);
```

**4** *Effects:* Equivalent to: `return append(s);`

```cpp
constexpr basic_string& operator+=(charT c);
```

**5** *Effects:* Equivalent to: `return append(size_type{1}, c);`

```cpp
constexpr basic_string& operator+=(initializer_list<charT> il);
```

**6** *Effects:* Equivalent to: `return append(il);`
