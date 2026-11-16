# 30.9.2 Non-member operators [re.submatch.op]

[Contents](../../README.md) · up: [30.9 Class template `sub_match` \[re.submatch\]](re.submatch.md) · prev: [30.9.1 Members \[re.submatch.members\]](re.submatch.members.md) · next: [30.10 Class template `match_results` \[re.results\]](re.results.md)

**1** Let `SM-CAT(I)` be

```cpp
compare_three_way_result_t<basic_string<typename iterator_traits<I>::value_type>>
```

```cpp
template<class BiIter>
  bool operator==(const sub_match<BiIter>& lhs, const sub_match<BiIter>& rhs);
```

**2** *Returns:* `lhs.compare(rhs) == 0`.

```cpp
template<class BiIter>
  auto operator<=>(const sub_match<BiIter>& lhs, const sub_match<BiIter>& rhs);
```

**3** *Returns:* `static_cast<SM-CAT(BiIter)>(lhs.compare(rhs) <=> 0)`.

```cpp
template<class BiIter, class ST, class SA>
  bool operator==(
      const sub_match<BiIter>& lhs,
      const basic_string<typename iterator_traits<BiIter>::value_type, ST, SA>& rhs);
```

**4** *Returns:*

```cpp
lhs.compare(typename sub_match<BiIter>::string_type(rhs.data(), rhs.size())) == 0
```

```cpp
template<class BiIter, class ST, class SA>
  auto operator<=>(
      const sub_match<BiIter>& lhs,
      const basic_string<typename iterator_traits<BiIter>::value_type, ST, SA>& rhs);
```

**5** *Returns:*

```cpp
static_cast<SM-CAT(BiIter)>(lhs.compare(
    typename sub_match<BiIter>::string_type(rhs.data(), rhs.size()))
      <=> 0
    )
```

```cpp
template<class BiIter>
  bool operator==(const sub_match<BiIter>& lhs,
                  const typename iterator_traits<BiIter>::value_type* rhs);
```

**6** *Returns:* `lhs.compare(rhs) == 0`.

```cpp
template<class BiIter>
  auto operator<=>(const sub_match<BiIter>& lhs,
                   const typename iterator_traits<BiIter>::value_type* rhs);
```

**7** *Returns:* `static_cast<SM-CAT(BiIter)>(lhs.compare(rhs) <=> 0)`.

```cpp
template<class BiIter>
  bool operator==(const sub_match<BiIter>& lhs,
                  const typename iterator_traits<BiIter>::value_type& rhs);
```

**8** *Returns:* `lhs.compare(typename sub_match<BiIter>::string_type(1, rhs)) == 0`.

```cpp
template<class BiIter>
  auto operator<=>(const sub_match<BiIter>& lhs,
                   const typename iterator_traits<BiIter>::value_type& rhs);
```

**9** *Returns:*

```cpp
static_cast<SM-CAT(BiIter)>(lhs.compare(
    typename sub_match<BiIter>::string_type(1, rhs))
      <=> 0
    )
```

```cpp
template<class charT, class ST, class BiIter>
  basic_ostream<charT, ST>&
    operator<<(basic_ostream<charT, ST>& os, const sub_match<BiIter>& m);
```

**10** *Returns:* `os << m.str()`.
