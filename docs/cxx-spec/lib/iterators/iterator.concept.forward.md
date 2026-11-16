# 23.3.4.11 Concept `forward_iterator` [iterator.concept.forward]

[Contents](../../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.10 Concept `output_iterator` \[iterator.concept.output\]](iterator.concept.output.md) · next: [23.3.4.12 Concept `bidirectional_iterator` \[iterator.concept.bidir\]](iterator.concept.bidir.md)

**1** The `forward_iterator` concept adds copyability, equality comparison, and the multi-pass guarantee, specified below.

```cpp
template<class I>
  concept forward_iterator =
    input_iterator<I> &&
    derived_from<ITER_CONCEPT(I), forward_iterator_tag> &&
    incrementable<I> &&
    sentinel_for<I, I>;
```

**2** The domain of `==` for forward iterators is that of iterators over the same underlying sequence. However, value-initialized iterators of the same type may be compared and shall compare equal to other value-initialized iterators of the same type. [*Note 1*: Value-initialized iterators behave as if they refer past the end of the same empty sequence. — *end note*]

**3** Pointers and references obtained from a forward iterator into a range `[i, s)` shall remain valid while `[i, s)` continues to denote a range.

**4** Two dereferenceable iterators `a` and `b` of type `X` offer the *multi-pass guarantee* if:

- `a == b` implies `++a == ++b` and
- The expression `((void)[](X x){++x;}(a), *a)` is equivalent to the expression `*a`.

**5** [*Note 2*: The requirement that `a == b` implies `++a == ++b` and the removal of the restrictions on the number of assignments through a mutable iterator (which applies to output iterators) allow the use of multi-pass one-directional algorithms with forward iterators. — *end note*]
