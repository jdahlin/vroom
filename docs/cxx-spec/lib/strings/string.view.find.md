# 21.4.2.7 Searching [string.view.find]

[Contents](../../README.md) · up: [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md) · prev: [21.4.2.6 String operations \[string.view.ops\]](string.view.ops.md) · next: [21.4.3 Deduction guide \[string.view.deduct\]](string.view.deduct.md)

**1** Member functions in this subclause have complexity 𝑂(`size() * str.size()`) at worst, although implementations should do better.

**2** Let *F* be one of `find`, `rfind`, `find_first_of`, `find_last_of`, `find_first_not_of`, and `find_last_not_of`.

- Each member function of the form

  ```cpp
  constexpr return-type F(const charT* s, size_type pos) const;
  ```

  has effects equivalent to: `return F(basic_string_view(s), pos);`

- Each member function of the form

  ```cpp
  constexpr return-type F(const charT* s, size_type pos, size_type n) const;
  ```

  has effects equivalent to: `return F(basic_string_view(s, n), pos);`

- Each member function of the form

  ```cpp
  constexpr return-type F(charT c, size_type pos) const noexcept;
  ```

  has effects equivalent to: `return F(basic_string_view(addressof(c), 1), pos);`

```cpp
constexpr size_type find(basic_string_view str, size_type pos = 0) const noexcept;
```

**3** Let `xpos` be the lowest position, if possible, such that the following conditions hold:

- `pos <= xpos`
- `xpos + str.size() <= size()`
- `traits::eq(at(xpos + I), str.at(I))` for all elements `I` of the string referenced by `str`.

**4** *Effects:* Determines `xpos`.

**5** *Returns:* `xpos` if the function can determine such a value for `xpos`. Otherwise, returns `npos`.

```cpp
constexpr size_type rfind(basic_string_view str, size_type pos = npos) const noexcept;
```

**6** Let `xpos` be the highest position, if possible, such that the following conditions hold:

- `xpos <= pos`
- `xpos + str.size() <= size()`
- `traits::eq(at(xpos + I), str.at(I))` for all elements `I` of the string referenced by `str`.

**7** *Effects:* Determines `xpos`.

**8** *Returns:* `xpos` if the function can determine such a value for `xpos`. Otherwise, returns `npos`.

```cpp
constexpr size_type find_first_of(basic_string_view str, size_type pos = 0) const noexcept;
```

**9** Let `xpos` be the lowest position, if possible, such that the following conditions hold:

- `pos <= xpos`
- `xpos < size()`
- `traits::eq(at(xpos), str.at(I))` for some element `I` of the string referenced by `str`.

**10** *Effects:* Determines `xpos`.

**11** *Returns:* `xpos` if the function can determine such a value for `xpos`. Otherwise, returns `npos`.

```cpp
constexpr size_type find_last_of(basic_string_view str, size_type pos = npos) const noexcept;
```

**12** Let `xpos` be the highest position, if possible, such that the following conditions hold:

- `xpos <= pos`
- `xpos < size()`
- `traits::eq(at(xpos), str.at(I))` for some element `I` of the string referenced by `str`.

**13** *Effects:* Determines `xpos`.

**14** *Returns:* `xpos` if the function can determine such a value for `xpos`. Otherwise, returns `npos`.

```cpp
constexpr size_type find_first_not_of(basic_string_view str, size_type pos = 0) const noexcept;
```

**15** Let `xpos` be the lowest position, if possible, such that the following conditions hold:

- `pos <= xpos`
- `xpos < size()`
- `traits::eq(at(xpos), str.at(I))` for no element `I` of the string referenced by `str`.

**16** *Effects:* Determines `xpos`.

**17** *Returns:* `xpos` if the function can determine such a value for `xpos`. Otherwise, returns `npos`.

```cpp
constexpr size_type find_last_not_of(basic_string_view str, size_type pos = npos) const noexcept;
```

**18** Let `xpos` be the highest position, if possible, such that the following conditions hold:

- `xpos <= pos`
- `xpos < size()`
- `traits::eq(at(xpos), str.at(I))` for no element `I` of the string referenced by `str`.

**19** *Effects:* Determines `xpos`.

**20** *Returns:* `xpos` if the function can determine such a value for `xpos`. Otherwise, returns `npos`.
