# 30.10.4 Element access [re.results.acc]

[Contents](../README.md) · up: [30.10 Class template `match_results` \[re.results\]](re.results.md) · prev: [30.10.3 Size \[re.results.size\]](re.results.size.md) · next: [30.10.5 Formatting \[re.results.form\]](re.results.form.md)

```cpp
difference_type length(size_type sub = 0) const;
```

**1** *Preconditions:* `ready() == true`.

**2** *Returns:* `(*this)[sub].length()`.

```cpp
difference_type position(size_type sub = 0) const;
```

**3** *Preconditions:* `ready() == true`.

**4** *Returns:* The distance from the start of the target sequence to `(*this)[sub].first`.

```cpp
string_type str(size_type sub = 0) const;
```

**5** *Preconditions:* `ready() == true`.

**6** *Returns:* `string_type((*this)[sub])`.

```cpp
const_reference operator[](size_type n) const;
```

**7** *Preconditions:* `ready() == true`.

**8** *Returns:* A reference to the `sub_match` object representing the character sequence that matched marked sub-expression `n`. If `n == 0` then returns a reference to a `sub_match` object representing the character sequence that matched the whole regular expression. If `n >= size()` then returns a `sub_match` object representing an unmatched sub-expression.

```cpp
const_reference prefix() const;
```

**9** *Preconditions:* `ready() == true`.

**10** *Returns:* A reference to the `sub_match` object representing the character sequence from the start of the string being matched/searched to the start of the match found.

```cpp
const_reference suffix() const;
```

**11** *Preconditions:* `ready() == true`.

**12** *Returns:* A reference to the `sub_match` object representing the character sequence from the end of the match found to the end of the string being matched/searched.

```cpp
const_iterator begin() const;
const_iterator cbegin() const;
```

**13** *Returns:* A starting iterator that enumerates over all the sub-expressions stored in `*this`.

```cpp
const_iterator end() const;
const_iterator cend() const;
```

**14** *Returns:* A terminating iterator that enumerates over all the sub-expressions stored in `*this`.
