# 21.4.2.6 String operations [string.view.ops]

[Contents](../README.md) · up: [21.4.2 Class template `basic_string_view` \[string.view.template\]](string.view.template.md) · prev: [21.4.2.5 Modifiers \[string.view.modifiers\]](string.view.modifiers.md) · next: [21.4.2.7 Searching \[string.view.find\]](string.view.find.md)

```cpp
constexpr size_type copy(charT* s, size_type n, size_type pos = 0) const;
```

**1** Let `rlen` be the smaller of `n` and `size() - pos`.

**2** *Throws:* `out_of_range` if `pos > size()`.

**3** *Preconditions:* `[s, s + rlen)` is a valid range.

**4** *Effects:* Equivalent to `traits::copy(s, data() + pos, rlen)`.

**5** *Returns:* `rlen`.

**6** *Complexity:* 𝑂(`rlen`).

```cpp
constexpr basic_string_view substr(size_type pos = 0, size_type n = npos) const;
```

**7** Let `rlen` be the smaller of `n` and `size() - pos`.

**8** *Throws:* `out_of_range` if `pos > size()`.

**9** *Effects:* Determines `rlen`, the effective length of the string to reference.

**10** *Returns:* `basic_string_view(data() + pos, rlen)`.

```cpp
constexpr int compare(basic_string_view str) const noexcept;
```

**11** Let `rlen` be the smaller of `size()` and `str.size()`.

**12** *Effects:* Determines `rlen`, the effective length of the strings to compare. The function then compares the two strings by calling `traits::compare(data(), str.data(), rlen)`.

**13** *Complexity:* 𝑂(`rlen`).

**14** *Returns:* The nonzero result if the result of the comparison is nonzero. Otherwise, returns a value as indicated in [Table 70](string.view.ops.md#tab:string.view.compare).

<a id="tab:string.view.compare"></a>
**Table 70: `compare()` results** [tab:string.view.compare]

| Condition | Return Value |
| --- | --- |
| `size() < str.size()` | `< 0` |
| `size() == str.size()` | `0` |
| `size() > str.size()` | `> 0` |

```cpp
constexpr int compare(size_type pos1, size_type n1, basic_string_view str) const;
```

**15** *Effects:* Equivalent to: `return substr(pos1, n1).compare(str);`

```cpp
constexpr int compare(size_type pos1, size_type n1, basic_string_view str,
                      size_type pos2, size_type n2) const;
```

**16** *Effects:* Equivalent to: `return substr(pos1, n1).compare(str.substr(pos2, n2));`

```cpp
constexpr int compare(const charT* s) const;
```

**17** *Effects:* Equivalent to: `return compare(basic_string_view(s));`

```cpp
constexpr int compare(size_type pos1, size_type n1, const charT* s) const;
```

**18** *Effects:* Equivalent to: `return substr(pos1, n1).compare(basic_string_view(s));`

```cpp
constexpr int compare(size_type pos1, size_type n1, const charT* s, size_type n2) const;
```

**19** *Effects:* Equivalent to: `return substr(pos1, n1).compare(basic_string_view(s, n2));`

```cpp
constexpr bool starts_with(basic_string_view x) const noexcept;
```

**20** *Effects:* Equivalent to: `return substr(0, x.size()) == x;`

```cpp
constexpr bool starts_with(charT x) const noexcept;
```

**21** *Effects:* Equivalent to: `return !empty() && traits::eq(front(), x);`

```cpp
constexpr bool starts_with(const charT* x) const;
```

**22** *Effects:* Equivalent to: `return starts_with(basic_string_view(x));`

```cpp
constexpr bool ends_with(basic_string_view x) const noexcept;
```

**23** *Effects:* Equivalent to:

```cpp
return size() >= x.size() && compare(size() - x.size(), npos, x) == 0;
```

```cpp
constexpr bool ends_with(charT x) const noexcept;
```

**24** *Effects:* Equivalent to: `return !empty() && traits::eq(back(), x);`

```cpp
constexpr bool ends_with(const charT* x) const;
```

**25** *Effects:* Equivalent to: `return ends_with(basic_string_view(x));`
