# 23.3.5 C++17 iterator requirements [iterator.cpp17]

[Contents](../../README.md) · up: [23.3 Iterator requirements \[iterator.requirements\]](iterator.requirements.md) · prev: [23.3.4.14 Concept `contiguous_iterator` \[iterator.concept.contiguous\]](iterator.concept.contiguous.md) · next: [23.3.5.1 *Cpp17Iterator* \[iterator.iterators\]](iterator.iterators.md)

**1** In the following sections, `a` and `b` denote values of type `X` or `const X`, `difference_type` and `reference` refer to the types `iterator_traits<X>::difference_type` and `iterator_traits<X>::reference`, respectively, `n` denotes a value of `difference_type`, `u`, `tmp`, and `m` denote identifiers, `r` denotes a value of `X&`, `t` denotes a value of value type `T`, `o` denotes a value of some type that is writable to the output iterator. [*Note 1*: For an iterator type `X` there must be an instantiation of `iterator_traits<X>` ([[iterator.traits]](iterator.traits.md)). — *end note*]

## Subclauses

- [23.3.5.1 *Cpp17Iterator* \[iterator.iterators\]](iterator.iterators.md)
- [23.3.5.2 Input iterators \[input.iterators\]](input.iterators.md)
- [23.3.5.3 Output iterators \[output.iterators\]](output.iterators.md)
- [23.3.5.4 Forward iterators \[forward.iterators\]](forward.iterators.md)
- [23.3.5.5 Bidirectional iterators \[bidirectional.iterators\]](bidirectional.iterators.md)
- [23.3.5.6 Random access iterators \[random.access.iterators\]](random.access.iterators.md)
