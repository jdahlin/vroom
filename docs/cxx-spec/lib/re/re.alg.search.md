# 30.11.3 `regex_search` [re.alg.search]

[Contents](../../README.md) · up: [30.11 Regular expression algorithms \[re.alg\]](re.alg.md) · prev: [30.11.2 `regex_match` \[re.alg.match\]](re.alg.match.md) · next: [30.11.4 `regex_replace` \[re.alg.replace\]](re.alg.replace.md)

```cpp
template<class BidirectionalIterator, class Allocator, class charT, class traits>
  bool regex_search(BidirectionalIterator first, BidirectionalIterator last,
                    match_results<BidirectionalIterator, Allocator>& m,
                    const basic_regex<charT, traits>& e,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
```

**1** *Preconditions:* `BidirectionalIterator` meets the *Cpp17BidirectionalIterator* requirements ([[bidirectional.iterators]](../iterators/bidirectional.iterators.md)).

**2** *Effects:* Determines whether there is some sub-sequence within `[first, last)` that matches the regular expression `e`. The parameter `flags` is used to control how the expression is matched against the character sequence. Returns `true` if such a sequence exists, `false` otherwise.

**3** *Postconditions:* `m.ready() == true` in all cases. If the function returns `false`, then the effect on parameter `m` is unspecified except that `m.size()` returns `0` and `m.empty()` returns `true`. Otherwise the effects on parameter `m` are given in [Table 142](re.alg.search.md#tab:re.alg.search).

<a id="tab:re.alg.search"></a>
**Table 142: Effects of `regex_search` algorithm** [tab:re.alg.search]

| Element | Value |
| --- | --- |
| `m.size()` | `1 + e.mark_count()` |
| `m.empty()` | `false` |
| `m.prefix().first` | `first` |
| `m.prefix().second` | `m[0].first` |
| `m.prefix().matched` | `m.prefix().first != m.prefix().second` |
| `m.suffix().first` | `m[0].second` |
| `m.suffix().second` | `last` |
| `m.suffix().matched` | `m.suffix().first != m.suffix().second` |
| `m[0].first` | The start of the sequence of characters that matched the regular expression |
| `m[0].second` | The end of the sequence of characters that matched the regular expression |
| `m[0].matched` | `true` |
| `m[n].first` | For all integers `0 < n < m.size()`, the start of the sequence that matched sub-expression `n`. Alternatively, if sub-expression `n` did not participate in the match, then `last`. |
| `m[n].second` | For all integers `0 < n < m.size()`, the end of the sequence that matched sub-expression `n`. Alternatively, if sub-expression `n` did not participate in the match, then `last`. |
| `m[n].matched` | For all integers `0 < n < m.size()`, `true` if sub-expression `n` participated in the match, `false` otherwise. |

```cpp
template<class charT, class Allocator, class traits>
  bool regex_search(const charT* str, match_results<const charT*, Allocator>& m,
                    const basic_regex<charT, traits>& e,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
```

**4** *Returns:* `regex_search(str, str + char_traits<charT>::length(str), m, e, flags)`.

```cpp
template<class ST, class SA, class Allocator, class charT, class traits>
  bool regex_search(const basic_string<charT, ST, SA>& s,
                    match_results<typename basic_string<charT, ST, SA>::const_iterator,
                                  Allocator>& m,
                    const basic_regex<charT, traits>& e,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
```

**5** *Returns:* `regex_search(s.begin(), s.end(), m, e, flags)`.

```cpp
template<class BidirectionalIterator, class charT, class traits>
  bool regex_search(BidirectionalIterator first, BidirectionalIterator last,
                    const basic_regex<charT, traits>& e,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
```

**6** *Effects:* Behaves “as if” by constructing an object `what` of type `match_results<BidirectionalIterator>` and returning `regex_search(first, last, what, e, flags)`.

```cpp
template<class charT, class traits>
  bool regex_search(const charT* str,
                    const basic_regex<charT, traits>& e,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
```

**7** *Returns:* `regex_search(str, str + char_traits<charT>::length(str), e, flags)`.

```cpp
template<class ST, class SA, class charT, class traits>
  bool regex_search(const basic_string<charT, ST, SA>& s,
                    const basic_regex<charT, traits>& e,
                    regex_constants::match_flag_type flags = regex_constants::match_default);
```

**8** *Returns:* `regex_search(s.begin(), s.end(), e, flags)`.
