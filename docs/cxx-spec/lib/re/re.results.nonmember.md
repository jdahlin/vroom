# 30.10.8 Non-member functions [re.results.nonmember]

[Contents](../../README.md) · up: [30.10 Class template `match_results` \[re.results\]](re.results.md) · prev: [30.10.7 Swap \[re.results.swap\]](re.results.swap.md) · next: [30.11 Regular expression algorithms \[re.alg\]](re.alg.md)

```cpp
template<class BidirectionalIterator, class Allocator>
bool operator==(const match_results<BidirectionalIterator, Allocator>& m1,
                const match_results<BidirectionalIterator, Allocator>& m2);
```

**1** *Returns:* `true` if neither match result is ready, `false` if one match result is ready and the other is not. If both match results are ready, returns `true` only if:

- `m1.empty() && m2.empty()`, or

- `!m1.empty() && !m2.empty()`, and the following conditions are satisfied:

  - `m1.prefix() == m2.prefix()`,
  - `m1.size() == m2.size() && equal(m1.begin(), m1.end(), m2.begin())`, and
  - `m1.suffix() == m2.suffix()`.

[*Note 1*: The algorithm `equal` is defined in [[algorithms]](../algorithms/algorithms.md). — *end note*]
