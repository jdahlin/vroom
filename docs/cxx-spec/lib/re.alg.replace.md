# 30.11.4 `regex_replace` [re.alg.replace]

[Contents](../README.md) · up: [30.11 Regular expression algorithms \[re.alg\]](re.alg.md) · prev: [30.11.3 `regex_search` \[re.alg.search\]](re.alg.search.md) · next: [30.12 Regular expression iterators \[re.iter\]](re.iter.md)

```cpp
template<class OutputIterator, class BidirectionalIterator,
          class traits, class charT, class ST, class SA>
  OutputIterator
    regex_replace(OutputIterator out,
                  BidirectionalIterator first, BidirectionalIterator last,
                  const basic_regex<charT, traits>& e,
                  const basic_string<charT, ST, SA>& fmt,
                  regex_constants::match_flag_type flags = regex_constants::match_default);
template<class OutputIterator, class BidirectionalIterator, class traits, class charT>
  OutputIterator
    regex_replace(OutputIterator out,
                  BidirectionalIterator first, BidirectionalIterator last,
                  const basic_regex<charT, traits>& e,
                  const charT* fmt,
                  regex_constants::match_flag_type flags = regex_constants::match_default);
```

**1** *Effects:* Constructs a `regex_iterator` object `i` as if by

```cpp
regex_iterator<BidirectionalIterator, charT, traits> i(first, last, e, flags)
```

and uses `i` to enumerate through all of the matches `m` of type `match_results<BidirectionalIterator>` that occur within the sequence `[first, last)`. If no such matches are found and `!(flags & regex_constants::format_no_copy)`, then calls

```cpp
out = copy(first, last, out)
```

If any matches are found then, for each such match:

- If `!(flags & regex_constants::format_no_copy)`, calls

  ```cpp
  out = copy(m.prefix().first, m.prefix().second, out)
  ```

- Then calls

  ```cpp
  out = m.format(out, fmt, flags)
  ```

  for the first form of the function and

  ```cpp
  out = m.format(out, fmt, fmt + char_traits<charT>::length(fmt), flags)
  ```

  for the second.

Finally, if such a match is found and `!(flags & regex_constants::format_no_copy)`, calls

```cpp
out = copy(last_m.suffix().first, last_m.suffix().second, out)
```

where `last_m` is a copy of the last match found. If `flags & regex_constants::format_first_only` is nonzero, then only the first match found is replaced.

**2** *Returns:* `out`.

```cpp
template<class traits, class charT, class ST, class SA, class FST, class FSA>
  basic_string<charT, ST, SA>
    regex_replace(const basic_string<charT, ST, SA>& s,
                  const basic_regex<charT, traits>& e,
                  const basic_string<charT, FST, FSA>& fmt,
                  regex_constants::match_flag_type flags = regex_constants::match_default);
template<class traits, class charT, class ST, class SA>
  basic_string<charT, ST, SA>
    regex_replace(const basic_string<charT, ST, SA>& s,
                  const basic_regex<charT, traits>& e,
                  const charT* fmt,
                  regex_constants::match_flag_type flags = regex_constants::match_default);
```

**3** *Effects:* Constructs an empty string `result` of type `basic_string<charT, ST, SA>` and calls:

```cpp
regex_replace(back_inserter(result), s.begin(), s.end(), e, fmt, flags);
```

**4** *Returns:* `result`.

```cpp
template<class traits, class charT, class ST, class SA>
  basic_string<charT>
    regex_replace(const charT* s,
                  const basic_regex<charT, traits>& e,
                  const basic_string<charT, ST, SA>& fmt,
                  regex_constants::match_flag_type flags = regex_constants::match_default);
template<class traits, class charT>
  basic_string<charT>
    regex_replace(const charT* s,
                  const basic_regex<charT, traits>& e,
                  const charT* fmt,
                  regex_constants::match_flag_type flags = regex_constants::match_default);
```

**5** *Effects:* Constructs an empty string `result` of type `basic_string<charT>` and calls:

```cpp
regex_replace(back_inserter(result), s, s + char_traits<charT>::length(s), e, fmt, flags);
```

**6** *Returns:* `result`.
