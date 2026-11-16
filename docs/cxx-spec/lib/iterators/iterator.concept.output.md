# 23.3.4.10 Concept `output_iterator` [iterator.concept.output]

[Contents](../../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.9 Concept `input_iterator` \[iterator.concept.input\]](iterator.concept.input.md) · next: [23.3.4.11 Concept `forward_iterator` \[iterator.concept.forward\]](iterator.concept.forward.md)

**1** The `output_iterator` concept defines requirements for a type that can be used to write values (from the requirement for `indirectly_writable` ([[iterator.concept.writable]](iterator.concept.writable.md))) and which can be both pre- and post-incremented. [*Note 1*: Output iterators are not required to model `equality_comparable`. — *end note*]

```cpp
template<class I, class T>
  concept output_iterator =
    input_or_output_iterator<I> &&
    indirectly_writable<I, T> &&
    requires(I i, T&& t) {
      *i++ = std::forward<T>(t);        // not required to be equality-preserving
    };
```

**2** Let `E` be an expression such that `decltype((E))` is `T`, and let `i` be a dereferenceable object of type `I`. `I` and `T` model `output_iterator<I, T>` only if `*i++ = E;` has effects equivalent to:

```cpp
*i = E;
++i;
```

**3** [*Note 2*: Algorithms on output iterators should never attempt to pass through the same iterator twice. They should be single-pass algorithms. — *end note*]
