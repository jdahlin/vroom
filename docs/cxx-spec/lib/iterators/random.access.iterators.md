# 23.3.5.6 Random access iterators [random.access.iterators]

[Contents](../../README.md) · up: [23.3.5 C++17 iterator requirements \[iterator.cpp17\]](iterator.cpp17.md) · prev: [23.3.5.5 Bidirectional iterators \[bidirectional.iterators\]](bidirectional.iterators.md) · next: [23.3.6 Indirect callable requirements \[indirectcallable\]](indirectcallable.md)

**1** A class or pointer type `X` meets the requirements of a random access iterator if, in addition to meeting the *Cpp17BidirectionalIterator* requirements, the following expressions are valid as shown in [Table 89](random.access.iterators.md#tab:randomaccessiterator).

<a id="tab:randomaccessiterator"></a>
**Table 89: *Cpp17RandomAccessIterator* requirements (in addition to *Cpp17BidirectionalIterator*)** [tab:randomaccessiterator]

| Expression | Return type | Operational | Assertion/note |
| --- | --- | --- | --- |
|  |  | semantics | pre-/post-condition |
| `r += n` | `X&` | `{ difference_type m = n;`<br>`if (m >= 0)`<br>`while (m--)`<br>`++r;`<br>`else`<br>`while (m++)`<br>`--r;`<br>`return r; }` |  |
| `a + n`<br>`n + a` | `X` | `{ X tmp = a;`<br>`return tmp += n; }` | `a + n == n + a`. |
| `r -= n` | `X&` | `return r += -n;` | *Preconditions:* the absolute value of `n` is in the range of representable values of `difference_type`. |
| `a - n` | `X` | `{ X tmp = a;`<br>`return tmp -= n; }` |  |
| `b - a` | `difference_type` | `return n` | *Preconditions:* there exists a value `n` of type `difference_type` such that `a + n == b`.<br>`b == a + (b - a)`. |
| `a[n]` | convertible to `reference` | `*(a + n)` |  |
| `a < b` | contextually convertible to `bool` | `b - a > 0` | `<` is a total ordering relation |
| `a > b` | contextually convertible to `bool` | `b < a` | `>` is a total ordering relation opposite to `<`. |
| `a >= b` | contextually convertible to `bool` | `!(a < b)` |  |
| `a <= b` | contextually convertible to `bool`. | `!(a > b)` |  |
