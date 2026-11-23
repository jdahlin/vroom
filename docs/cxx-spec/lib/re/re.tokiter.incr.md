# 30.12.2.4 Increment [re.tokiter.incr]

[Contents](../../README.md) · up: [30.12.2 Class template `regex_token_iterator` \[re.tokiter\]](re.tokiter.md) · prev: [30.12.2.3 Indirection \[re.tokiter.deref\]](re.tokiter.deref.md) · next: [30.13 Modified ECMAScript regular expression grammar \[re.grammar\]](re.grammar.md)

```cpp
regex_token_iterator& operator++();
```

**1** *Effects:* Constructs a local variable `prev` of type `position_iterator`, initialized with the value of `position`.

**2** If `*this` is a suffix iterator, sets `*this` to an end-of-sequence iterator.

**3** Otherwise, if `N + 1 < subs.size()`, increments `N` and sets `result` to the address of the current match.

**4** Otherwise, sets `N` to 0 and increments `position`. If `position` is not an end-of-sequence iterator the operator sets `result` to the address of the current match.

**5** Otherwise, if any of the values stored in `subs` is equal to -1 and `prev->suffix().length()` is not 0 the operator sets `*this` to a suffix iterator that points to the range `[prev->suffix().first, prev->suffix().second)`.

**6** Otherwise, sets `*this` to an end-of-sequence iterator.

**7** *Returns:* `*this`

```cpp
regex_token_iterator& operator++(int);
```

**8** *Effects:* Constructs a copy `tmp` of `*this`, then calls `++(*this)`.

**9** *Returns:* `tmp`.
