# 23.3.5.3 Output iterators [output.iterators]

[Contents](../README.md) · up: [23.3.5 C++17 iterator requirements \[iterator.cpp17\]](iterator.cpp17.md) · prev: [23.3.5.2 Input iterators \[input.iterators\]](input.iterators.md) · next: [23.3.5.4 Forward iterators \[forward.iterators\]](forward.iterators.md)

**1** A class or pointer type `X` meets the requirements of an output iterator if `X` meets the *Cpp17Iterator* requirements ([[iterator.iterators]](iterator.iterators.md)) and the expressions in [Table 86](output.iterators.md#tab:outputiterator) are valid and have the indicated semantics.

<a id="tab:outputiterator"></a>
**Table 86: *Cpp17OutputIterator* requirements (in addition to *Cpp17Iterator*)** [tab:outputiterator]

| Expression | Return type | Operational | Assertion/note |
| --- | --- | --- | --- |
|  |  | semantics | pre-/post-condition |
| `*r = o` | result is not used |  | *Remarks:* After this operation `r` is not required to be dereferenceable.<br>*Postconditions:* `r` is incrementable. |
| `++r` | `X&` |  | `addressof(r) == addressof(++r)`.<br>*Remarks:* After this operation `r` is not required to be dereferenceable.<br>*Postconditions:* `r` is incrementable. |
| `r++` | convertible to `const X&` | `{ X tmp = r;`<br>`++r;`<br>`return tmp; }` | *Remarks:* After this operation `r` is not required to be dereferenceable.<br>*Postconditions:* `r` is incrementable. |
| `*r++ = o` | result is not used |  | *Remarks:* After this operation `r` is not required to be dereferenceable.<br>*Postconditions:* `r` is incrementable. |

**2** [*Note 1*: The only valid use of an `operator*` is on the left side of the assignment statement. Assignment through the same value of the iterator happens only once. Algorithms on output iterators should never attempt to pass through the same iterator twice. They should be single-pass algorithms. Equality and inequality might not be defined. — *end note*]
