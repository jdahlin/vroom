# 30.8.2 Assignment [re.regex.assign]

[Contents](../README.md) · up: [30.8 Class template `basic_regex` \[re.regex\]](re.regex.md) · prev: [30.8.1 Constructors \[re.regex.construct\]](re.regex.construct.md) · next: [30.8.3 Constant operations \[re.regex.operations\]](re.regex.operations.md)

```cpp
basic_regex& operator=(const basic_regex& e);
```

**1** *Postconditions:* `flags()` and `mark_count()` return `e.flags()` and `e.mark_count()`, respectively.

```cpp
basic_regex& operator=(basic_regex&& e) noexcept;
```

**2** *Postconditions:* `flags()` and `mark_count()` return the values that `e.flags()` and `e.mark_count()`, respectively, had before assignment. `e` is in a valid state with unspecified value.

```cpp
basic_regex& operator=(const charT* p);
```

**3** *Effects:* Equivalent to: `return assign(p);`

```cpp
basic_regex& operator=(initializer_list<charT> il);
```

**4** *Effects:* Equivalent to: `return assign(il.begin(), il.end());`

```cpp
template<class ST, class SA>
  basic_regex& operator=(const basic_string<charT, ST, SA>& s);
```

**5** *Effects:* Equivalent to: `return assign(s);`

```cpp
basic_regex& assign(const basic_regex& e);
```

**6** *Effects:* Equivalent to: `return *this = e;`

```cpp
basic_regex& assign(basic_regex&& e) noexcept;
```

**7** *Effects:* Equivalent to: `return *this = std::move(e);`

```cpp
basic_regex& assign(const charT* p, flag_type f = regex_constants::ECMAScript);
```

**8** *Effects:* Equivalent to: `return assign(string_type(p), f);`

```cpp
basic_regex& assign(const charT* p, size_t len, flag_type f = regex_constants::ECMAScript);
```

**9** *Effects:* Equivalent to: `return assign(string_type(p, len), f);`

```cpp
template<class ST, class SA>
  basic_regex& assign(const basic_string<charT, ST, SA>& s,
                      flag_type f = regex_constants::ECMAScript);
```

**10** *Returns:* `*this`.

**11** *Effects:* Assigns the regular expression contained in the string `s`, interpreted according the flags specified in `f`. If an exception is thrown, `*this` is unchanged.

**12** *Postconditions:* If no exception is thrown, `flags()` returns `f` and `mark_count()` returns the number of marked sub-expressions within the expression.

**13** *Throws:* `regex_error` if `s` is not a valid regular expression.

```cpp
template<class InputIterator>
  basic_regex& assign(InputIterator first, InputIterator last,
                      flag_type f = regex_constants::ECMAScript);
```

**14** *Effects:* Equivalent to: `return assign(string_type(first, last), f);`

```cpp
basic_regex& assign(initializer_list<charT> il,
                    flag_type f = regex_constants::ECMAScript);
```

**15** *Effects:* Equivalent to: `return assign(il.begin(), il.end(), f);`
