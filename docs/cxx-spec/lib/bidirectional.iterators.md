# 23.3.5.5 Bidirectional iterators [bidirectional.iterators]

[Contents](../README.md) · up: [23.3.5 C++17 iterator requirements \[iterator.cpp17\]](iterator.cpp17.md) · prev: [23.3.5.4 Forward iterators \[forward.iterators\]](forward.iterators.md) · next: [23.3.5.6 Random access iterators \[random.access.iterators\]](random.access.iterators.md)

**1** A class or pointer type `X` meets the requirements of a bidirectional iterator if, in addition to meeting the *Cpp17ForwardIterator* requirements, the following expressions are valid as shown in [Table 88](bidirectional.iterators.md#tab:bidirectionaliterator).

<a id="tab:bidirectionaliterator"></a>
**Table 88: *Cpp17BidirectionalIterator* requirements (in addition to *Cpp17ForwardIterator*)** [tab:bidirectionaliterator]

| Expression | Return type | Operational | Assertion/note |
| --- | --- | --- | --- |
|  |  | semantics | pre-/post-condition |
| `--r` | `X&` |  | *Preconditions:* there exists `s` such that `r == ++s`.<br>*Postconditions:* `r` is dereferenceable.<br>`--(++r) == r`.<br>`--r == --s` implies `r == s`.<br>`addressof(r) == addressof(--r)`. |
| `r--` | convertible to `const X&` | `{ X tmp = r;`<br>`--r;`<br>`return tmp; }` |  |
| `*r--` | `reference` |  |  |

**2** [*Note 1*: Bidirectional iterators allow algorithms to move iterators backward as well as forward. — *end note*]
