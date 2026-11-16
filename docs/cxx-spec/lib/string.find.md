# 21.3.2.7.2 Searching [string.find]

[Contents](../README.md) · up: [21.3.2.7 String operations \[string.ops\]](string.ops.md) · prev: [21.3.2.7.1 Accessors \[string.accessors\]](string.accessors.md) · next: [21.3.2.7.3 `basic_string::substr` \[string.substr\]](string.substr.md)

**1** Let *F* be one of `find`, `rfind`, `find_first_of`, `find_last_of`, `find_first_not_of`, and `find_last_not_of`.

- Each member function of the form

  ```cpp
  constexpr size_type F(const basic_string& str, size_type pos) const noexcept;
  ```

  has effects equivalent to: `return F(basic_string_view<charT, traits>(str), pos);`

- Each member function of the form

  ```cpp
  constexpr size_type F(const charT* s, size_type pos) const;
  ```

  has effects equivalent to: `return F(basic_string_view<charT, traits>(s), pos);`

- Each member function of the form

  ```cpp
  constexpr size_type F(const charT* s, size_type pos, size_type n) const;
  ```

  has effects equivalent to: `return F(basic_string_view<charT, traits>(s, n), pos);`

- Each member function of the form

  ```cpp
  constexpr size_type F(charT c, size_type pos) const noexcept;
  ```

  has effects equivalent to:

  ```cpp
  return F(basic_string_view<charT, traits>(addressof(c), 1), pos);
  ```

```cpp
template<class T>
  constexpr size_type find(const T& t, size_type pos = 0) const noexcept(see below);
template<class T>
  constexpr size_type rfind(const T& t, size_type pos = npos) const noexcept(see below);
template<class T>
  constexpr size_type find_first_of(const T& t, size_type pos = 0) const noexcept(see below);
template<class T>
  constexpr size_type find_last_of(const T& t, size_type pos = npos) const noexcept(see below);
template<class T>
  constexpr size_type find_first_not_of(const T& t, size_type pos = 0) const noexcept(see below);
template<class T>
  constexpr size_type find_last_not_of(const T& t, size_type pos = npos) const noexcept(see below);
```

**2** *Constraints:*

- `is_convertible_v<const T&, basic_string_view<charT, traits>>` is `true` and
- `is_convertible_v<const T&, const charT*>` is `false`.

**3** *Effects:* Let *G* be the name of the function. Equivalent to:

```cpp
basic_string_view<charT, traits> s = *this, sv = t;
return s.G(sv, pos);
```

**4** *Remarks:* The expression inside `noexcept` is equivalent to `is_nothrow_convertible_v<const T&, basic_string_view<charT, traits>>`.
