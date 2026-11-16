# 12.4.3.1.4 Reference binding [over.ics.ref]

[Contents](README.md) · up: [12.4.3.1 Implicit conversion sequences \[over.best.ics\]](over.best.ics.md) · prev: [12.4.3.1.3 Ellipsis conversion sequences \[over.ics.ellipsis\]](over.ics.ellipsis.md) · next: [12.4.3.1.5 List-initialization sequence \[over.ics.list\]](over.ics.list.md)

**1** When a parameter of reference type binds directly ([[dcl.init.ref]](dcl.init.ref.md)) to an argument expression, the implicit conversion sequence is the identity conversion, unless the argument expression has a type that is a derived class of the parameter type, in which case the implicit conversion sequence is a derived-to-base Conversion ([[over.best.ics]](over.best.ics.md)). [*Example 1*:

```cpp
struct A {};
struct B : public A {} b;
int f(A&);
int f(B&);
int i = f(b);       // calls f(B&), an exact match, rather than f(A&), a conversion
```

— *end example*] If the parameter binds directly to the result of applying a conversion function to the argument expression, the implicit conversion sequence is a user-defined conversion sequence ([[over.ics.user]](over.ics.user.md)), with the second standard conversion sequence either an identity conversion or, if the conversion function returns an entity of a type that is a derived class of the parameter type, a derived-to-base conversion.

**2** When a parameter of reference type is not bound directly to an argument expression, the conversion sequence is the one required to convert the argument expression to the referenced type according to [[over.best.ics]](over.best.ics.md). Conceptually, this conversion sequence corresponds to copy-initializing a temporary of the referenced type with the argument expression. Any difference in top-level cv-qualification is subsumed by the initialization itself and does not constitute a conversion.

**3** Except for an implicit object parameter, for which see [[over.match.funcs]](over.match.funcs.md), an implicit conversion sequence cannot be formed if it requires binding an lvalue reference other than a reference to a non-volatile `const` type to an rvalue or binding an rvalue reference to an lvalue other than a function lvalue. [*Note 1*: This means, for example, that a candidate function cannot be a viable function if it has a non-`const` lvalue reference parameter (other than the implicit object parameter) and the corresponding argument would require a temporary to be created to initialize the lvalue reference (see [[dcl.init.ref]](dcl.init.ref.md)). — *end note*]

**4** Other restrictions on binding a reference to a particular argument that are not based on the types of the reference and the argument do not affect the formation of an implicit conversion sequence, however. [*Example 2*: A function with an “lvalue reference to `int`” parameter can be a viable candidate even if the corresponding argument is an `int` bit-field. The formation of implicit conversion sequences treats the `int` bit-field as an `int` lvalue and finds an exact match with the parameter. If the function is selected by overload resolution, the call will nonetheless be ill-formed because of the prohibition on binding a non-`const` lvalue reference to a bit-field ([[dcl.init.ref]](dcl.init.ref.md)). — *end example*]
