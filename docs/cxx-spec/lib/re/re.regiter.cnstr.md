# 30.12.1.1 Constructors [re.regiter.cnstr]

[Contents](../../README.md) · up: [30.12.1 Class template `regex_iterator` \[re.regiter\]](re.regiter.md) · prev: [30.12.1 Class template `regex_iterator` \[re.regiter\]](re.regiter.md) · next: [30.12.1.2 Comparisons \[re.regiter.comp\]](re.regiter.comp.md)

```cpp
regex_iterator();
```

**1** *Effects:* Constructs an end-of-sequence iterator.

```cpp
regex_iterator(BidirectionalIterator a, BidirectionalIterator b,
               const regex_type& re,
               regex_constants::match_flag_type m = regex_constants::match_default);
```

**2** *Effects:* Initializes `begin` and `end` to `a` and `b`, respectively, sets `pregex` to `addressof(re)`, sets `flags` to `m`, then calls `regex_search(begin, end, match, *pregex, flags)`. If this call returns `false` the constructor sets `*this` to the end-of-sequence iterator.
