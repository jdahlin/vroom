# 30.10.7 Swap [re.results.swap]

[Contents](../../README.md) · up: [30.10 Class template `match_results` \[re.results\]](re.results.md) · prev: [30.10.6 Allocator \[re.results.all\]](re.results.all.md) · next: [30.10.8 Non-member functions \[re.results.nonmember\]](re.results.nonmember.md)

```cpp
void swap(match_results& that);
```

**1** *Effects:* Swaps the contents of the two sequences.

**2** *Postconditions:* `*this` contains the sequence of matched sub-expressions that were in `that`, `that` contains the sequence of matched sub-expressions that were in `*this`.

**3** *Complexity:* Constant time.

```cpp
template<class BidirectionalIterator, class Allocator>
  void swap(match_results<BidirectionalIterator, Allocator>& m1,
            match_results<BidirectionalIterator, Allocator>& m2);
```

**4** *Effects:* As if by `m1.swap(m2)`.
