# 30.12.2.1 Constructors [re.tokiter.cnstr]

[Contents](../../README.md) · up: [30.12.2 Class template `regex_token_iterator` \[re.tokiter\]](re.tokiter.md) · prev: [30.12.2 Class template `regex_token_iterator` \[re.tokiter\]](re.tokiter.md) · next: [30.12.2.2 Comparisons \[re.tokiter.comp\]](re.tokiter.comp.md)

```cpp
regex_token_iterator();
```

**1** *Effects:* Constructs the end-of-sequence iterator.

```cpp
regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                     const regex_type& re,
                     int submatch = 0,
                     regex_constants::match_flag_type m = regex_constants::match_default);

regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                     const regex_type& re,
                     const vector<int>& submatches,
                     regex_constants::match_flag_type m = regex_constants::match_default);

regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                     const regex_type& re,
                     initializer_list<int> submatches,
                     regex_constants::match_flag_type m = regex_constants::match_default);

template<size_t N>
  regex_token_iterator(BidirectionalIterator a, BidirectionalIterator b,
                       const regex_type& re,
                       const int (&submatches)[N],
                       regex_constants::match_flag_type m = regex_constants::match_default);
```

**2** *Preconditions:* Each of the initialization values of `submatches` is `>= -1`.

**3** *Effects:* The first constructor initializes the member `subs` to hold the single value `submatch`. The second, third, and fourth constructors initialize the member `subs` to hold a copy of the sequence of integer values pointed to by the iterator range `[begin(submatches), end(submatches))`.

**4** Each constructor then sets `N` to 0, and `position` to `position_iterator(a, b, re, m)`. If `position` is not an end-of-sequence iterator the constructor sets `result` to the address of the current match. Otherwise if any of the values stored in `subs` is equal to -1 the constructor sets `*this` to a suffix iterator that points to the range `[a, b)`, otherwise the constructor sets `*this` to an end-of-sequence iterator.
