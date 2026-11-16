# 25.11.1 Special memory concepts [special.mem.concepts]

[Contents](../../README.md) · up: [25.11 Specialized `<memory>` algorithms \[specialized.algorithms\]](specialized.algorithms.md) · prev: [25.11 Specialized `<memory>` algorithms \[specialized.algorithms\]](specialized.algorithms.md) · next: [25.11.2 `uninitialized_default_construct` \[uninitialized.construct.default\]](uninitialized.construct.default.md)

**1** Some algorithms in this subclause are constrained with the following exposition-only concepts:

```cpp
template<class I>
concept no-throw-input-iterator = // exposition only
  input_iterator<I> &&
  is_lvalue_reference_v<iter_reference_t<I>> &&
  same_as<remove_cvref_t<iter_reference_t<I>>, iter_value_t<I>>;
```

**2** A type `I` models `no-throw-input-iterator` only if no exceptions are thrown from increment, copy construction, move construction, copy assignment, move assignment, or indirection through valid iterators.

**3** [*Note 1*: This concept allows some `input_iterator` ([[iterator.concept.input]](../iterators/iterator.concept.input.md)) operations to throw exceptions. — *end note*]

```cpp
template<class S, class I>
concept no-throw-sentinel = sentinel_for<S, I>; // exposition only
```

**4** Types `S` and `I` model `no-throw-sentinel` only if no exceptions are thrown from copy construction, move construction, copy assignment, move assignment, or comparisons between valid values of type `I` and `S`.

**5** [*Note 2*: This concept allows some `sentinel_for` ([[iterator.concept.sentinel]](../iterators/iterator.concept.sentinel.md)) operations to throw exceptions. — *end note*]

```cpp
template<class R>
concept no-throw-input-range = // exposition only
  range<R> &&
  no-throw-input-iterator<iterator_t<R>> &&
  no-throw-sentinel<sentinel_t<R>, iterator_t<R>>;
```

**6** A type `R` models `no-throw-input-range` only if no exceptions are thrown from calls to `ranges::begin` and `ranges::end` on an object of type `R`.

```cpp
template<class I>
concept no-throw-forward-iterator = // exposition only
  no-throw-input-iterator<I> &&
  forward_iterator<I> &&
  no-throw-sentinel<I, I>;
```

**7** [*Note 3*: This concept allows some `forward_iterator` ([[iterator.concept.forward]](../iterators/iterator.concept.forward.md)) operations to throw exceptions. — *end note*]

```cpp
template<class R>
concept no-throw-forward-range = // exposition only
  no-throw-input-range<R> &&
  no-throw-forward-iterator<iterator_t<R>>;
```
