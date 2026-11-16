# 11.11.1 Defaulted comparison operator functions [class.compare.default]

[Contents](README.md) · up: [11.11 Comparisons \[class.compare\]](class.compare.md) · prev: [11.11 Comparisons \[class.compare\]](class.compare.md) · next: [11.11.2 Equality operator \[class.eq\]](class.eq.md)

**1** A defaulted comparison operator function ([[over.binary]](over.binary.md)) for some class `C` shall be a non-template function that is

- a non-static const non-volatile member of `C` having one parameter of type `const C&` and either no *ref-qualifier* or the *ref-qualifier* `&`, or
- a friend of `C` having either two parameters of type `const C&` or two parameters of type `C`.

A comparison operator function for class `C` that is defaulted on its first declaration and is not defined as deleted is *implicitly defined* when it is odr-used or needed for constant evaluation. Name lookups in the defaulted definition of a comparison operator function are performed from a context equivalent to its *function-body*. A definition of a comparison operator as defaulted that appears in a class shall be the first declaration of that function.

**2** A defaulted `<=>` or `==` operator function for class `C` is defined as deleted if any non-static data member of `C` is of reference type or `C` has variant members ([[class.union.anon]](class.union.anon.md)).

**3** A binary operator expression `a @ b` is *usable* if either

- `a` or `b` is of class or enumeration type and overload resolution ([[over.match]](over.match.md)) as applied to `a @ b` results in a usable candidate, or
- neither `a` nor `b` is of class or enumeration type and `a @ b` is a valid expression.

**4** A defaulted comparison function is *constexpr-compatible* if it satisfies the requirements for a constexpr function ([[dcl.constexpr]](dcl.constexpr.md)) and no overload resolution performed when determining whether to delete the function results in a usable candidate that is a non-constexpr function. [*Note 1*: This includes the overload resolutions performed:

- for an `operator<=>` whose return type is not `auto`, when determining whether a synthesized three-way comparison is defined,
- for an `operator<=>` whose return type is `auto` or for an `operator==`, for a comparison between an element of the expanded list of subobjects and itself, or
- for a secondary comparison operator `@`, for the expression `x @ y`.

— *end note*]

**5** If the *member-specification* does not explicitly declare any member or friend named `operator==`, an `==` operator function is declared implicitly for each three-way comparison operator function defined as defaulted in the *member-specification*, with the same access and *function-definition* and in the same class scope as the respective three-way comparison operator function, except that the return type is replaced with `bool` and the *declarator-id* is replaced with `operator==`. [*Note 2*: Such an implicitly-declared `==` operator for a class `X` is defined as defaulted in the definition of `X` and has the same *parameter-declaration-clause* and trailing *requires-clause* as the respective three-way comparison operator. It is declared with `friend`, `virtual`, `constexpr`, or `consteval` if the three-way comparison operator function is so declared. If the three-way comparison operator function has no *noexcept-specifier*, the implicitly-declared `==` operator function has an implicit exception specification ([[except.spec]](except.spec.md)) that may differ from the implicit exception specification of the three-way comparison operator function. — *end note*] [*Example 1*:

```cpp
template<typename T> struct X {
  friend constexpr std::partial_ordering operator<=>(X, X) requires (sizeof(T) != 1) = default;
  // implicitly declares: friend constexpr bool operator==(X, X) requires (sizeof(T) != 1) = default;

  [[nodiscard]] virtual std::strong_ordering operator<=>(const X&) const = default;
  // implicitly declares: [[nodiscard]] virtual bool operator==(const X&) const = default;
};
```

— *end example*] [*Note 3*: The `==` operator function is declared implicitly even if the defaulted three-way comparison operator function is defined as deleted. — *end note*]

**6** The direct base class subobjects of `C`, in the order of their declaration in the *base-specifier-list* of `C`, followed by the non-static data members of `C`, in the order of their declaration in the *member-specification* of `C`, form a list of subobjects. In that list, any subobject of array type is recursively expanded to the sequence of its elements, in the order of increasing subscript. Let `x`<sub>*i*</sub> be an lvalue denoting the *i*<sup></sup>*th* element in the expanded list of subobjects for an object `x` (of length *n*), where `x`<sub>*i*</sub> is formed by a sequence of derived-to-base conversions ([[over.best.ics]](over.best.ics.md)), class member access expressions ([[expr.ref]](expr.ref.md)), and array subscript expressions ([[expr.sub]](expr.sub.md)) applied to `x`.
