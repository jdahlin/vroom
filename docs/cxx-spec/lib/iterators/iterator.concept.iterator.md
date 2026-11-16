# 23.3.4.6 Concept `input_or_output_iterator` [iterator.concept.iterator]

[Contents](../../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.5 Concept `incrementable` \[iterator.concept.inc\]](iterator.concept.inc.md) · next: [23.3.4.7 Concept `sentinel_for` \[iterator.concept.sentinel\]](iterator.concept.sentinel.md)

**1** The `input_or_output_iterator` concept forms the basis of the iterator concept taxonomy; every iterator models `input_or_output_iterator`. This concept specifies operations for dereferencing and incrementing an iterator. Most algorithms will require additional operations to compare iterators with sentinels ([[iterator.concept.sentinel]](iterator.concept.sentinel.md)), to read ([[iterator.concept.input]](iterator.concept.input.md)) or write ([[iterator.concept.output]](iterator.concept.output.md)) values, or to provide a richer set of iterator movements ([[iterator.concept.forward]](iterator.concept.forward.md), [[iterator.concept.bidir]](iterator.concept.bidir.md), [[iterator.concept.random.access]](iterator.concept.random.access.md)).

```cpp
template<class I>
  concept input_or_output_iterator =
    requires(I i) {
      { *i } -> can-reference;
    } &&
    weakly_incrementable<I>;
```

**2** [*Note 1*: Unlike the *Cpp17Iterator* requirements, the `input_or_output_iterator` concept does not require copyability. — *end note*]
