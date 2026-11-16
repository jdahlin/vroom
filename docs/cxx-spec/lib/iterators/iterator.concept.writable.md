# 23.3.4.3 Concept `indirectly_writable` [iterator.concept.writable]

[Contents](../../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.2 Concept `indirectly_readable` \[iterator.concept.readable\]](iterator.concept.readable.md) · next: [23.3.4.4 Concept `weakly_incrementable` \[iterator.concept.winc\]](iterator.concept.winc.md)

**1** The `indirectly_writable` concept specifies the requirements for writing a value into an iterator’s referenced object.

```cpp
template<class Out, class T>
  concept indirectly_writable =
    requires(Out&& o, T&& t) {
      *o = std::forward<T>(t);  // not required to be equality-preserving
      *std::forward<Out>(o) = std::forward<T>(t);   // not required to be equality-preserving
      const_cast<const iter_reference_t<Out>&&>(*o) =
        std::forward<T>(t);     // not required to be equality-preserving
      const_cast<const iter_reference_t<Out>&&>(*std::forward<Out>(o)) =
        std::forward<T>(t);     // not required to be equality-preserving
    };
```

**2** Let `E` be an expression such that `decltype((E))` is `T`, and let `o` be a dereferenceable object of type `Out`. `Out` and `T` model `indirectly_writable<Out, T>` only if

- If `Out` and `T` model `indirectly_readable<Out> && same_as<iter_value_t<Out>, decay_t<T>>`, then `*o` after any above assignment is equal to the value of `E` before the assignment.

**3** After evaluating any above assignment expression, `o` is not required to be dereferenceable.

**4** If `E` is an xvalue ([[basic.lval]](../../expr/basic.lval.md)), the resulting state of the object it denotes is valid but unspecified ([[lib.types.movedfrom]](../library/lib.types.movedfrom.md)).

**5** [*Note 1*: The only valid use of an `operator*` is on the left side of the assignment statement. Assignment through the same value of the indirectly writable type happens only once. — *end note*]

**6** [*Note 2*: `indirectly_writable` has the awkward `const_cast` expressions to reject iterators with prvalue non-proxy reference types that permit rvalue assignment but do not also permit `const` rvalue assignment. Consequently, an iterator type `I` that returns `std::string` by value does not model `indirectly_writable<I, std::string>`. — *end note*]
