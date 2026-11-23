# 23.3.4.4 Concept `weakly_incrementable` [iterator.concept.winc]

[Contents](../../README.md) · up: [23.3.4 Iterator concepts \[iterator.concepts\]](iterator.concepts.md) · prev: [23.3.4.3 Concept `indirectly_writable` \[iterator.concept.writable\]](iterator.concept.writable.md) · next: [23.3.4.5 Concept `incrementable` \[iterator.concept.inc\]](iterator.concept.inc.md)

**1** The `weakly_incrementable` concept specifies the requirements on types that can be incremented with the pre- and post-increment operators. The increment operations are not required to be equality-preserving, nor is the type required to be `equality_comparable`.

```cpp
template<class T>
  inline constexpr bool is-integer-like = see below;            // exposition only

template<class T>
  inline constexpr bool is-signed-integer-like = see below;     // exposition only

template<class I>
  concept weakly_incrementable =
    default_initializable<I> && movable<I> &&
    requires(I i) {
      typename iter_difference_t<I>;
      requires is-signed-integer-like<iter_difference_t<I>>;
      { ++i } -> same_as<I&>;   // not required to be equality-preserving
      i++;                      // not required to be equality-preserving
    };
```

**2** A type `I` is an *integer-class type* if it is in a set of implementation-defined class types that behave as integer types do, as defined in below.

**3** The range of representable values of an integer-class type is the continuous set of values over which it is defined. The values 0 and 1 are part of the range of every integer-class type. If any negative numbers are part of the range, the type is a *signed-integer-class type*; otherwise, it is an *unsigned-integer-class type*.

**4** For every integer-class type `I`, let `B(I)` be a hypothetical extended integer type of the same signedness with the smallest width ([[basic.fundamental]](../../basic/basic.fundamental.md)) capable of representing the same range of values. The width of `I` is equal to the width of `B(I)`.

**5** Let `a` and `b` be objects of integer-class type `I`, let `x` and `y` be objects of type `B(I)` as described above that represent the same values as `a` and `b` respectively, and let `c` be an lvalue of any integral type.

- For every unary operator `@` for which the expression `@x` is well-formed, `@a` shall also be well-formed and have the same value, effects, and value category as `@x` provided that value is representable by `I`. If `@x` has type `bool`, so too does `@a`; if `@x` has type `B(I)`, then `@a` has type `I`.
- For every assignment operator `@=` for which `c @= x` is well-formed, `c @= a` shall also be well-formed and shall have the same value and effects as `c @= x`. The expression `c @= a` shall be an lvalue referring to `c`.
- For every binary operator `@` for which `x @ y` is well-formed, `a @ b` shall also be well-formed and shall have the same value, effects, and value category as `x @ y` provided that value is representable by `I`. If `x @ y` has type `bool`, so too does `a @ b`; if `x @ y` has type `B(I)`, then `a @ b` has type `I`.

**6** Expressions of integer-class type are explicitly convertible to any integral type. Expressions of integral type are both implicitly and explicitly convertible to any integer-class type. Conversions between integral and integer-class types do not exit via an exception.

**7** An expression `E` of integer-class type `I` is contextually convertible to `bool` as if by `bool(E != I(0))`.

**8** All integer-class types model `regular` ([[concepts.object]](../concepts/concepts.object.md)) and `totally_ordered` ([[concept.totallyordered]](../concepts/concept.totallyordered.md)).

**9** A value-initialized object of integer-class type has value 0.

**10** For every (possibly cv-qualified) integer-class type `I`, `numeric_limits<I>` is specialized such that:

- `numeric_limits<I>::is_specialized` is `true`,
- `numeric_limits<I>::is_signed` is `true` if and only if `I` is a signed-integer-class type,
- `numeric_limits<I>::is_integer` is `true`,
- `numeric_limits<I>::is_exact` is `true`,
- `numeric_limits<I>::digits` is equal to the width of the integer-class type,
- `numeric_limits<I>::digits10` is equal to `static_cast<int>(digits * log10(2))`, and
- `numeric_limits<I>::min()` and `numeric_limits<I>::max()` return the lowest and highest representable values of `I`, respectively, and `numeric_limits<I>::lowest()` returns `numeric_limits<I>::min()`.

**11** A type `I` is *integer-like* if it models `integral<I>` or if it is an integer-class type. A type `I` is *signed-integer-like* if it models `signed_integral<I>` or if it is a signed-integer-class type. A type `I` is *unsigned-integer-like* if it models `unsigned_integral<I>` or if it is an unsigned-integer-class type.

**12** `is-integer-like<I>` is `true` if and only if `I` is an integer-like type. `is-signed-integer-like<I>` is `true` if and only if I is a signed-integer-like type.

**13** Let `i` be an object of type `I`. When `i` is in the domain of both pre- and post-increment, `i` is said to be *incrementable*. `I` models `weakly_incrementable<I>` only if

- The expressions `++i` and `i++` have the same domain.
- If `i` is incrementable, then both `++i` and `i++` advance `i` to the next element.
- If `i` is incrementable, then `addressof(++i)` is equal to `addressof(i)`.

**14** [*Note 1*: For `weakly_incrementable` types, `a` equals `b` does not imply that `++a` equals `++b`. (Equality does not guarantee the substitution property or referential transparency.) Algorithms on weakly incrementable types should never attempt to pass through the same incrementable value twice. They should be single-pass algorithms. These algorithms can be used with istreams as the source of the input data through the `istream_iterator` class template. — *end note*]
