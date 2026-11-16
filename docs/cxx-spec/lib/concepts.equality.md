# 18.2 Equality preservation [concepts.equality]

[Contents](../README.md) · up: [18 Concepts library \[concepts\]](concepts.md) · prev: [18.1 General \[concepts.general\]](concepts.general.md) · next: [18.3 Header `<concepts>` synopsis \[concepts.syn\]](concepts.syn.md)

**1** An expression is *equality-preserving* if, given equal inputs, the expression results in equal outputs. The inputs to an expression are the set of the expression’s operands. The output of an expression is the expression’s result and all operands modified by the expression. For the purposes of this subclause, the operands of an expression are the largest subexpressions that include only:

- an *id-expression* ([[expr.prim.id]](../expr.prim.id.md)), and
- invocations of the library function templates `std::move`, `std::forward`, and `std::declval` ([[forward]](forward.md), [[declval]](declval.md)).

[*Example 1*: The operands of the expression `a = std::move(b)` are `a` and `std::move(b)`. — *end example*]

**2** Not all input values need be valid for a given expression; e.g., for integers `a` and `b`, the expression `a / b` is not well-defined when `b` is `0`. This does not preclude the expression `a / b` being equality-preserving. The *domain* of an expression is the set of input values for which the expression is required to be well-defined.

**3** Expressions required by this document to be equality-preserving are further required to be stable: two evaluations of such an expression with the same input objects are required to have equal outputs absent any explicit intervening modification of those input objects. [*Note 1*: This requirement allows generic code to reason about the current values of objects based on knowledge of the prior values as observed via equality-preserving expressions. It effectively forbids spontaneous changes to an object, changes to an object from another thread of execution, changes to an object as side effects of non-modifying expressions, and changes to an object as side effects of modifying a distinct object if those changes could be observable to a library function via an equality-preserving expression that is required to be valid for that object. — *end note*]

**4** Expressions declared in a *requires-expression* in this document are required to be equality-preserving, except for those annotated with the comment “not required to be equality-preserving.” An expression so annotated may be equality-preserving, but is not required to be so.

**5** An expression that may alter the value of one or more of its inputs in a manner observable to equality-preserving expressions is said to modify those inputs. This document uses a notational convention to specify which expressions declared in a *requires-expression* modify which inputs: except where otherwise specified, an expression operand that is a non-constant lvalue or rvalue may be modified. Operands that are constant lvalues or rvalues are required to not be modified. For the purposes of this subclause, the cv-qualification and value category of each operand are determined by assuming that each template type parameter denotes a cv-unqualified complete non-array object type.

**6** Where a *requires-expression* declares an expression that is non-modifying for some constant lvalue operand, additional variations of that expression that accept a non-constant lvalue or (possibly constant) rvalue for the given operand are also required except where such an expression variation is explicitly required with differing semantics. These *implicit expression variations* are required to meet the semantic requirements of the declared expression. The extent to which an implementation validates the syntax of the variations is unspecified.

**7** [*Example 2*:

```cpp
template<class T> concept C = requires(T a, T b, const T c, const T d) {
  c == d;           // #1
  a = std::move(b); // #2
  a = c;            // #3
};
```

For the above example:

- Expression #1 does not modify either of its operands, #2 modifies both of its operands, and #3 modifies only its first operand `a`.

- Expression #1 implicitly requires additional expression variations that meet the requirements for `c == d` (including non-modification), as if the expressions

  ```cpp
                                              c  ==           b;
            c  == std::move(d);               c  == std::move(b);
  std::move(c) ==           d;      std::move(c) ==           b;
  std::move(c) == std::move(d);     std::move(c) == std::move(b);

            a  ==           d;                a  ==           b;
            a  == std::move(d);               a  == std::move(b);
  std::move(a) ==           d;      std::move(a) ==           b;
  std::move(a) == std::move(d);     std::move(a) == std::move(b);
  ```

  had been declared as well.

- Expression #3 implicitly requires additional expression variations that meet the requirements for `a = c` (including non-modification of the second operand), as if the expressions `a = b` and `a = std::move(c)` had been declared. Expression #3 does not implicitly require an expression variation with a non-constant rvalue second operand, since expression #2 already specifies exactly such an expression explicitly.

— *end example*]

**8** [*Example 3*: The following type `T` meets the explicitly stated syntactic requirements of concept `C` above but does not meet the additional implicit requirements:

```cpp
struct T {
  bool operator==(const T&) const { return true; }
  bool operator==(T&) = delete;
};
```

`T` fails to meet the implicit requirements of `C`, so `T` satisfies but does not model `C`. Since implementations are not required to validate the syntax of implicit requirements, it is unspecified whether an implementation diagnoses as ill-formed a program that requires `C<T>`. — *end example*]
