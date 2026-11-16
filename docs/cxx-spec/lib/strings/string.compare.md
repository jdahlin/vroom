# 21.3.2.7.4 `basic_string::compare` [string.compare]

[Contents](../../README.md) · up: [21.3.2.7 String operations \[string.ops\]](string.ops.md) · prev: [21.3.2.7.3 `basic_string::substr` \[string.substr\]](string.substr.md) · next: [21.3.2.7.5 `basic_string::starts_with` \[string.starts.with\]](string.starts.with.md)

```cpp
template<class T>
  constexpr int compare(const T& t) const noexcept(see below);
```

**1** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**2** *Effects:* Equivalent to: `return basic_string_view<charT, traits>(*this).compare(t);`

**3** *Remarks:* The expression inside `noexcept` is equivalent to `is_nothrow_convertible_v<const T&, basic_string_view<charT, traits>>`.

```cpp
template<class T>
  constexpr int compare(size_type pos1, size_type n1, const T& t) const;
```

**4** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**5** *Effects:* Equivalent to:

```cpp
return basic_string_view<charT, traits>(*this).substr(pos1, n1).compare(t);
```

```cpp
template<class T>
  constexpr int compare(size_type pos1, size_type n1, const T& t,
                        size_type pos2, size_type n2 = npos) const;
```

**6** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**7** *Effects:* Equivalent to:

```cpp
basic_string_view<charT, traits> s = *this, sv = t;
return s.substr(pos1, n1).compare(sv.substr(pos2, n2));
```

```cpp
constexpr int compare(const basic_string& str) const noexcept;
```

**8** *Effects:* Equivalent to: `return compare(basic_string_view<charT, traits>(str));`

```cpp
constexpr int compare(size_type pos1, size_type n1, const basic_string& str) const;
```

**9** *Effects:* Equivalent to: `return compare(pos1, n1, basic_string_view<charT, traits>(str));`

```cpp
constexpr int compare(size_type pos1, size_type n1, const basic_string& str,
                      size_type pos2, size_type n2 = npos) const;
```

**10** *Effects:* Equivalent to:

```cpp
return compare(pos1, n1, basic_string_view<charT, traits>(str), pos2, n2);
```

```cpp
constexpr int compare(const charT* s) const;
```

**11** *Effects:* Equivalent to: `return compare(basic_string_view<charT, traits>(s));`

```cpp
constexpr int compare(size_type pos, size_type n1, const charT* s) const;
```

**12** *Effects:* Equivalent to: `return compare(pos, n1, basic_string_view<charT, traits>(s));`

```cpp
constexpr int compare(size_type pos, size_type n1, const charT* s, size_type n2) const;
```

**13** *Effects:* Equivalent to: `return compare(pos, n1, basic_string_view<charT, traits>(s, n2));`
