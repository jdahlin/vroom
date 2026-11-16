# 23.3.4.1 General [iterator.concepts.general]

[Contents](../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · next: [23.3.4.2 Concept `indirectly_readable` \[iterator.concept.readable\]](iterator.concept.readable.md)

**1** For a type `I`, let `ITER_TRAITS(I)` denote the type `I` if `iterator_traits<I>` names a specialization generated from the primary template. Otherwise, `ITER_TRAITS(I)` denotes `iterator_traits<I>`.

- If the *qualified-id* `ITER_TRAITS(I)::iterator_concept` is valid and names a type, then `ITER_CONCEPT(I)` denotes that type.
- Otherwise, if the *qualified-id* `ITER_TRAITS(I)::iterator_category` is valid and names a type, then `ITER_CONCEPT(I)` denotes that type.
- Otherwise, if `iterator_traits<I>` names a specialization generated from the primary template, then `ITER_CONCEPT(I)` denotes `random_access_iterator_tag`.
- Otherwise, `ITER_CONCEPT(I)` does not denote a type.

**2** [*Note 1*: `ITER_TRAITS` enables independent syntactic determination of an iterator’s category and concept. — *end note*] [*Example 1*:

```cpp
struct I {
  using value_type = int;
  using difference_type = int;

  int operator*() const;
  I& operator++();
  I operator++(int);
  I& operator--();
  I operator--(int);

  bool operator==(I) const;
  bool operator!=(I) const;
};
```

`iterator_traits<I>::iterator_category` denotes `input_iterator_tag`, and `ITER_CONCEPT(I)` denotes `random_access_iterator_tag`. — *end example*]
