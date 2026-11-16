# 7.3.1 Lvalue-to-rvalue conversion [conv.lval]

[Contents](README.md) · up: [7.3 Standard conversions \[conv\]](conv.md) · prev: [7.3 Standard conversions \[conv\]](conv.md) · next: [7.3.2 Array-to-pointer conversion \[conv.array\]](conv.array.md)

**1** A glvalue ([[basic.lval]](basic.lval.md)) of a non-function, non-array type `T` can be converted to a prvalue.[^1] If `T` is an incomplete type, a program that necessitates this conversion is ill-formed. If `T` is a non-class type, the type of the prvalue is the cv-unqualified version of `T`. Otherwise, the type of the prvalue is `T`.[^2]

**2** When an lvalue-to-rvalue conversion is applied to an expression *E*, and either

- *E* is not potentially evaluated, or
- the evaluation of *E* results in the evaluation of a member *E*<sub></sub>*x* of the set of potential results of *E*, and *E*<sub></sub>*x* names a variable `x` that is not odr-used by *E*<sub></sub>*x* ([[basic.def.odr]](basic.def.odr.md)),

the value contained in the referenced object is not accessed. [*Example 1*:

```cpp
struct S { int n; };
auto f() {
  S x { 1 };
  constexpr S y { 2 };
  return [&](bool b) { return (b ? y : x).n; };
}
auto g = f();
int m = g(false);   // undefined behavior: access of x.n outside its lifetime
int n = g(true);    // OK, does not access y.n
```

— *end example*]

**3** The result of the conversion is determined according to the following rules:

- If `T` is *cv* `std::nullptr_t`, the result is a null pointer constant ([[conv.ptr]](conv.ptr.md)). [*Note 1*: Since the conversion does not access the object to which the glvalue refers, there is no side effect even if `T` is volatile-qualified ([[intro.execution]](intro.execution.md)), and the glvalue can refer to an inactive member of a union ([[class.union]](class.union.md)). — *end note*]
- Otherwise, if `T` has a class type, the conversion copy-initializes the result object from the glvalue.
- Otherwise, if the object to which the glvalue refers contains an invalid pointer value ([[basic.stc.dynamic.deallocation]](basic.stc.dynamic.deallocation.md), [[basic.stc.dynamic.safety]](basic.stc.dynamic.safety.md)), the behavior is implementation-defined.
- Otherwise, the object indicated by the glvalue is read ([[defns.access]](intro.defs.md#defns.access)), and the value contained in the object is the prvalue result.

**4** [*Note 2*: See also [[basic.lval]](basic.lval.md). — *end note*]

[^1]: For historical reasons, this conversion is called the “lvalue-to-rvalue” conversion, even though that name does not accurately reflect the taxonomy of expressions described in [[basic.lval]](basic.lval.md).
[^2]: In C++ class and array prvalues can have cv-qualified types. This differs from ISO C, in which non-lvalues never have cv-qualified types.
