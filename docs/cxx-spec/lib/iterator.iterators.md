# 23.3.5.1 *Cpp17Iterator* [iterator.iterators]

[Contents](../README.md) · up: [23.3.5 C++17 iterator requirements \[iterator.cpp17\]](iterator.cpp17.md) · prev: [23.3.5 C++17 iterator requirements \[iterator.cpp17\]](iterator.cpp17.md) · next: [23.3.5.2 Input iterators \[input.iterators\]](input.iterators.md)

**1** The *Cpp17Iterator* requirements form the basis of the iterator taxonomy; every iterator meets the *Cpp17Iterator* requirements. This set of requirements specifies operations for dereferencing and incrementing an iterator. Most algorithms will require additional operations to read ([[input.iterators]](input.iterators.md)) or write ([[output.iterators]](output.iterators.md)) values, or to provide a richer set of iterator movements ([[forward.iterators]](forward.iterators.md), [[bidirectional.iterators]](bidirectional.iterators.md), [[random.access.iterators]](random.access.iterators.md)).

**2** A type `X` meets the *Cpp17Iterator* requirements if:

- `X` meets the *Cpp17CopyConstructible*, *Cpp17CopyAssignable*, and *Cpp17Destructible* requirements ([[utility.arg.requirements]](utility.arg.requirements.md)) and lvalues of type `X` are swappable ([[swappable.requirements]](swappable.requirements.md)), and
- `iterator_traits<X>::difference_type` is a signed integer type or `void`, and
- the expressions in [Table 84](iterator.iterators.md#tab:iterator) are valid and have the indicated semantics.

<a id="tab:iterator"></a>
**Table 84: *Cpp17Iterator* requirements** [tab:iterator]

| Expression | Return type | Operational | Assertion/note |
| --- | --- | --- | --- |
|  |  | semantics | pre-/post-condition |
| `*r` | unspecified |  | *Preconditions:* `r` is dereferenceable. |
| `++r` | `X&` |  |  |
