# 30.8.1 Constructors [re.regex.construct]

[Contents](../README.md) · up: [30.8 Class template `basic_regex` \[re.regex\]](re.regex.md) · prev: [30.8 Class template `basic_regex` \[re.regex\]](re.regex.md) · next: [30.8.2 Assignment \[re.regex.assign\]](re.regex.assign.md)

```cpp
basic_regex();
```

**1** *Postconditions:* `*this` does not match any character sequence.

```cpp
explicit basic_regex(const charT* p, flag_type f = regex_constants::ECMAScript);
```

**2** *Preconditions:* `[p, p + char_traits<charT>::length(p))` is a valid range.

**3** *Effects:* The object’s internal finite state machine is constructed from the regular expression contained in the sequence of characters `[p, p + char_traits<charT>::length(p))`, and interpreted according to the flags `f`.

**4** *Postconditions:* `flags()` returns `f`. `mark_count()` returns the number of marked sub-expressions within the expression.

**5** *Throws:* `regex_error` if `[p, p + char_traits<charT>::length(p))` is not a valid regular expression.

```cpp
basic_regex(const charT* p, size_t len, flag_type f = regex_constants::ECMAScript);
```

**6** *Preconditions:* `[p, p + len)` is a valid range.

**7** *Effects:* The object’s internal finite state machine is constructed from the regular expression contained in the sequence of characters `[p, p + len)`, and interpreted according the flags specified in `f`.

**8** *Postconditions:* `flags()` returns `f`. `mark_count()` returns the number of marked sub-expressions within the expression.

**9** *Throws:* `regex_error` if `[p, p + len)` is not a valid regular expression.

```cpp
basic_regex(const basic_regex& e);
```

**10** *Postconditions:* `flags()` and `mark_count()` return `e.flags()` and `e.mark_count()`, respectively.

```cpp
basic_regex(basic_regex&& e) noexcept;
```

**11** *Postconditions:* `flags()` and `mark_count()` return the values that `e.flags()` and `e.mark_count()`, respectively, had before construction.

```cpp
template<class ST, class SA>
  explicit basic_regex(const basic_string<charT, ST, SA>& s,
                       flag_type f = regex_constants::ECMAScript);
```

**12** *Effects:* The object’s internal finite state machine is constructed from the regular expression contained in the string `s`, and interpreted according to the flags specified in `f`.

**13** *Postconditions:* `flags()` returns `f`. `mark_count()` returns the number of marked sub-expressions within the expression.

**14** *Throws:* `regex_error` if `s` is not a valid regular expression.

```cpp
template<class ForwardIterator>
  basic_regex(ForwardIterator first, ForwardIterator last,
              flag_type f = regex_constants::ECMAScript);
```

**15** *Effects:* The object’s internal finite state machine is constructed from the regular expression contained in the sequence of characters `[first, last)`, and interpreted according to the flags specified in `f`.

**16** *Postconditions:* `flags()` returns `f`. `mark_count()` returns the number of marked sub-expressions within the expression.

**17** *Throws:* `regex_error` if the sequence `[first, last)` is not a valid regular expression.

```cpp
basic_regex(initializer_list<charT> il, flag_type f = regex_constants::ECMAScript);
```

**18** *Effects:* Same as `basic_regex(il.begin(), il.end(), f)`.
