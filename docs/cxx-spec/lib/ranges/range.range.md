# 24.4.2 Ranges [range.range]

[Contents](../../README.md) · up: [24.4 Range requirements \[range.req\]](range.req.md) · prev: [24.4.1 General \[range.req.general\]](range.req.general.md) · next: [24.4.3 Sized ranges \[range.sized\]](range.sized.md)

**1** The `range` concept defines the requirements of a type that allows iteration over its elements by providing an iterator and sentinel that denote the elements of the range.

```cpp
template<class T>
  concept range =
    requires(T& t) {
      ranges::begin(t);                         // sometimes equality-preserving (see below)
      ranges::end(t);
    };
```

**2** The required expressions `ranges::begin(t)` and `ranges::end(t)` of the `range` concept do not require implicit expression variations ([[concepts.equality]](../concepts/concepts.equality.md)).

**3** Given an expression `t` such that `decltype((t))` is `T&`, `T` models `range` only if

- `[ranges::begin(t), ranges::end(t))` denotes a range ([[iterator.requirements.general]](../iterators/iterator.requirements.general.md)),
- both `ranges::begin(t)` and `ranges::end(t)` are amortized constant time and non-modifying, and
- if the type of `ranges::begin(t)` models `forward_iterator`, `ranges::begin(t)` is equality-preserving.

**4** [*Note 1*: Equality preservation of both `ranges::begin` and `ranges::end` enables passing a `range` whose iterator type models `forward_iterator` to multiple algorithms and making multiple passes over the range by repeated calls to `ranges::begin` and `ranges::end`. Since `ranges::begin` is not required to be equality-preserving when the return type does not model `forward_iterator`, repeated calls might not return equal values or might not be well-defined; `ranges::begin` should be called at most once for such a range. — *end note*]

```cpp
template<class T>
  concept borrowed_range =
    range<T> &&
      (is_lvalue_reference_v<T> || enable_borrowed_range<remove_cvref_t<T>>);
```

**5** Given an expression `E` such that `decltype((E))` is `T`, `T` models `borrowed_range` only if the validity of iterators obtained from the object denoted by `E` is not tied to the lifetime of that object.

**6** [*Note 2*: Since the validity of iterators is not tied to the lifetime of an object whose type models `borrowed_range`, a function can accept arguments of such a type by value and return iterators obtained from it without danger of dangling. — *end note*]

```cpp
template<class>
  inline constexpr bool enable_borrowed_range = false;
```

**7** *Remarks:* Pursuant to [[namespace.std]](../library/namespace.std.md), users may specialize `enable_borrowed_range` for cv-unqualified program-defined types. Such specializations shall be usable in constant expressions ([[expr.const]](../../expr/expr.const.md)) and have type `const bool`.

**8** [*Example 1*: Each specialization `S` of class template `subrange` ([[range.subrange]](range.subrange.md)) models `borrowed_range` because

- `enable_borrowed_range<S>` is specialized to have the value `true`, and
- `S`’s iterators do not have validity tied to the lifetime of an `S` object because they are “borrowed” from some other range.

— *end example*]
