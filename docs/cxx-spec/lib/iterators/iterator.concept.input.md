# 23.3.4.9 Concept `input_iterator` [iterator.concept.input]

[Contents](../../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.8 Concept `sized_sentinel_for` \[iterator.concept.sizedsentinel\]](iterator.concept.sizedsentinel.md) · next: [23.3.4.10 Concept `output_iterator` \[iterator.concept.output\]](iterator.concept.output.md)

**1** The `input_iterator` concept defines requirements for a type whose referenced values can be read (from the requirement for `indirectly_readable` ([[iterator.concept.readable]](iterator.concept.readable.md))) and which can be both pre- and post-incremented. [*Note 1*: Unlike the *Cpp17InputIterator* requirements ([[input.iterators]](input.iterators.md)), the `input_iterator` concept does not need equality comparison since iterators are typically compared to sentinels. — *end note*]

```cpp
template<class I>
  concept input_iterator =
    input_or_output_iterator<I> &&
    indirectly_readable<I> &&
    requires { typename ITER_CONCEPT(I); } &&
    derived_from<ITER_CONCEPT(I), input_iterator_tag>;
```
