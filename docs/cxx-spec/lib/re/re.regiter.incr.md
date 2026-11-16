# 30.12.1.4 Increment [re.regiter.incr]

[Contents](../../README.md) · up: [30.12.1 Class template `regex_iterator` \[re.regiter\]](re.regiter.md) · prev: [30.12.1.3 Indirection \[re.regiter.deref\]](re.regiter.deref.md) · next: [30.12.2 Class template `regex_token_iterator` \[re.tokiter\]](re.tokiter.md)

```cpp
regex_iterator& operator++();
```

**1** *Effects:* Constructs a local variable `start` of type `BidirectionalIterator` and initializes it with the value of `match[0].second`.

**2** If the iterator holds a zero-length match and `start == end` the operator sets `*this` to the end-of-sequence iterator and returns `*this`.

**3** Otherwise, if the iterator holds a zero-length match, the operator calls:

```cpp
regex_search(start, end, match, *pregex,
             flags | regex_constants::match_not_null | regex_constants::match_continuous)
```

If the call returns `true` the operator returns `*this`. Otherwise the operator increments `start` and continues as if the most recent match was not a zero-length match.

**4** If the most recent match was not a zero-length match, the operator sets `flags` to `flags | regex_constants::match_prev_avail` and calls `regex_search(start, end, match, *pregex, flags)`. If the call returns `false` the iterator sets `*this` to the end-of-sequence iterator. The iterator then returns `*this`.

**5** In all cases in which the call to `regex_search` returns `true`, `match.prefix().first` shall be equal to the previous value of `match[0].second`, and for each index `i` in the half-open range `[0, match.size())` for which `match[i].matched` is `true`, `match.position(i)` shall return `distance(begin, match[i].first)`.

**6** [*Note 1*: This means that `match.position(i)` gives the offset from the beginning of the target sequence, which is often not the same as the offset from the sequence passed in the call to `regex_search`. — *end note*]

**7** It is unspecified how the implementation makes these adjustments.

**8** [*Note 2*: This means that a compiler may call an implementation-specific search function, in which case a program-defined specialization of `regex_search` will not be called. — *end note*]

```cpp
regex_iterator operator++(int);
```

**9** *Effects:* As if by:

```cpp
regex_iterator tmp = *this;
++(*this);
return tmp;
```
