# 30.11.2 `regex_match` [re.alg.match]

[Contents](../../README.md) · up: [30.11 Regular expression algorithms \[re.alg\]](re.alg.md) · prev: [30.11.1 Exceptions \[re.except\]](re.except.md) · next: [30.11.3 `regex_search` \[re.alg.search\]](re.alg.search.md)

```cpp
template<class BidirectionalIterator, class Allocator, class charT, class traits>
  bool regex_match(BidirectionalIterator first, BidirectionalIterator last,
                   match_results<BidirectionalIterator, Allocator>& m,
                   const basic_regex<charT, traits>& e,
                   regex_constants::match_flag_type flags = regex_constants::match_default);
```

**1** *Preconditions:* `BidirectionalIterator` meets the *Cpp17BidirectionalIterator* requirements ([[bidirectional.iterators]](../iterators/bidirectional.iterators.md)).

**2** *Effects:* Determines whether there is a match between the regular expression `e`, and all of the character sequence `[first, last)`. The parameter `flags` is used to control how the expression is matched against the character sequence. When determining if there is a match, only potential matches that match the entire character sequence are considered. Returns `true` if such a match exists, `false` otherwise. [*Example 1*:

```cpp
std::regex re("Get|GetValue");
std::cmatch m;
regex_search("GetValue", m, re);        // returns true, and m[0] contains "Get"
regex_match ("GetValue", m, re);        // returns true, and m[0] contains "GetValue"
regex_search("GetValues", m, re);       // returns true, and m[0] contains "Get"
regex_match ("GetValues", m, re);       // returns false
```

— *end example*]

**3** *Postconditions:* `m.ready() == true` in all cases. If the function returns `false`, then the effect on parameter `m` is unspecified except that `m.size()` returns `0` and `m.empty()` returns `true`. Otherwise the effects on parameter `m` are given in [Table 141](re.alg.match.md#tab:re.alg.match).

<a id="tab:re.alg.match"></a>
**Table 141: Effects of `regex_match` algorithm** [tab:re.alg.match]

| Element | Value |
| --- | --- |
| `m.size()` | `1 + e.mark_count()` |
| `m.empty()` | `false` |
| `m.prefix().first` | `first` |
| `m.prefix().second` | `first` |
| `m.prefix().matched` | `false` |
| `m.suffix().first` | `last` |
| `m.suffix().second` | `last` |
| `m.suffix().matched` | `false` |
| `m[0].first` | `first` |
| `m[0].second` | `last` |
| `m[0].matched` | `true` |
| `m[n].first` | For all integers `0 < n < m.size()`, the start of the sequence that matched sub-expression `n`. Alternatively, if sub-expression `n` did not participate in the match, then `last`. |
| `m[n].second` | For all integers `0 < n < m.size()`, the end of the sequence that matched sub-expression `n`. Alternatively, if sub-expression `n` did not participate in the match, then `last`. |
| `m[n].matched` | For all integers `0 < n < m.size()`, `true` if sub-expression `n` participated in the match, `false` otherwise. |

```cpp
template<class BidirectionalIterator, class charT, class traits>
  bool regex_match(BidirectionalIterator first, BidirectionalIterator last,
                   const basic_regex<charT, traits>& e,
                   regex_constants::match_flag_type flags = regex_constants::match_default);
```

**4** *Effects:* Behaves “as if” by constructing an instance of `match_results<BidirectionalIterator> what`, and then returning the result of `regex_match(first, last, what, e, flags)`.

```cpp
template<class charT, class Allocator, class traits>
  bool regex_match(const charT* str,
                   match_results<const charT*, Allocator>& m,
                   const basic_regex<charT, traits>& e,
                   regex_constants::match_flag_type flags = regex_constants::match_default);
```

**5** *Returns:* `regex_match(str, str + char_traits<charT>::length(str), m, e, flags)`.

```cpp
template<class ST, class SA, class Allocator, class charT, class traits>
  bool regex_match(const basic_string<charT, ST, SA>& s,
                   match_results<typename basic_string<charT, ST, SA>::const_iterator,
                                 Allocator>& m,
                   const basic_regex<charT, traits>& e,
                   regex_constants::match_flag_type flags = regex_constants::match_default);
```

**6** *Returns:* `regex_match(s.begin(), s.end(), m, e, flags)`.

```cpp
template<class charT, class traits>
  bool regex_match(const charT* str,
                   const basic_regex<charT, traits>& e,
                   regex_constants::match_flag_type flags = regex_constants::match_default);
```

**7** *Returns:* `regex_match(str, str + char_traits<charT>::length(str), e, flags)`

```cpp
template<class ST, class SA, class charT, class traits>
  bool regex_match(const basic_string<charT, ST, SA>& s,
                   const basic_regex<charT, traits>& e,
                   regex_constants::match_flag_type flags = regex_constants::match_default);
```

**8** *Returns:* `regex_match(s.begin(), s.end(), e, flags)`.
