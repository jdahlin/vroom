# 23.3.4.5 Concept `incrementable` [iterator.concept.inc]

[Contents](../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.4 Concept `weakly_incrementable` \[iterator.concept.winc\]](iterator.concept.winc.md) · next: [23.3.4.6 Concept `input_or_output_iterator` \[iterator.concept.iterator\]](iterator.concept.iterator.md)

**1** The `incrementable` concept specifies requirements on types that can be incremented with the pre- and post-increment operators. The increment operations are required to be equality-preserving, and the type is required to be `equality_comparable`. [*Note 1*: This supersedes the annotations on the increment expressions in the definition of `weakly_incrementable`. — *end note*]

```cpp
template<class I>
  concept incrementable =
    regular<I> &&
    weakly_incrementable<I> &&
    requires(I i) {
      { i++ } -> same_as<I>;
    };
```

**2** Let `a` and `b` be incrementable objects of type `I`. `I` models `incrementable` only if

- If `bool(a == b)` then `bool(a++ == b)`.
- If `bool(a == b)` then `bool(((void)a++, a) == ++b)`.

**3** [*Note 2*: The requirement that `a` equals `b` implies `++a` equals `++b` (which is not true for weakly incrementable types) allows the use of multi-pass one-directional algorithms with types that model `incrementable`. — *end note*]
