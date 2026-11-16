# 23.3.5.4 Forward iterators [forward.iterators]

[Contents](../README.md) · up: [23.3.5 C++17 iterator requirements \[iterator.cpp17\]](iterator.cpp17.md) · prev: [23.3.5.3 Output iterators \[output.iterators\]](output.iterators.md) · next: [23.3.5.5 Bidirectional iterators \[bidirectional.iterators\]](bidirectional.iterators.md)

**1** A class or pointer type `X` meets the requirements of a forward iterator if

- `X` meets the *Cpp17InputIterator* requirements ([[input.iterators]](input.iterators.md)),
- `X` meets the *Cpp17DefaultConstructible* requirements ([[utility.arg.requirements]](utility.arg.requirements.md)),
- if `X` is a mutable iterator, `reference` is a reference to `T`; if `X` is a constant iterator, `reference` is a reference to `const T`,
- the expressions in [Table 87](forward.iterators.md#tab:forwarditerator) are valid and have the indicated semantics, and
- objects of type `X` offer the multi-pass guarantee, described below.

**2** The domain of `==` for forward iterators is that of iterators over the same underlying sequence. However, value-initialized iterators may be compared and shall compare equal to other value-initialized iterators of the same type. [*Note 1*: Value-initialized iterators behave as if they refer past the end of the same empty sequence. — *end note*]

**3** Two dereferenceable iterators `a` and `b` of type `X` offer the *multi-pass guarantee* if:

- `a == b` implies `++a == ++b` and
- `X` is a pointer type or the expression `(void)++X(a), *a` is equivalent to the expression `*a`.

**4** [*Note 2*: The requirement that `a == b` implies `++a == ++b` (which is not true for input and output iterators) and the removal of the restrictions on the number of the assignments through a mutable iterator (which applies to output iterators) allows the use of multi-pass one-directional algorithms with forward iterators. — *end note*]

<a id="tab:forwarditerator"></a>
**Table 87: *Cpp17ForwardIterator* requirements (in addition to *Cpp17InputIterator*)** [tab:forwarditerator]

| Expression | Return type | Operational | Assertion/note |
| --- | --- | --- | --- |
|  |  | semantics | pre-/post-condition |
| `r++` | convertible to `const X&` | `{ X tmp = r;`<br>`++r;`<br>`return tmp; }` |  |
| `*r++` | `reference` |  |  |

**5** If `a` and `b` are equal, then either `a` and `b` are both dereferenceable or else neither is dereferenceable.

**6** If `a` and `b` are both dereferenceable, then `a == b` if and only if `*a` and `*b` are bound to the same object.
