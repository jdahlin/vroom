# 12.4.3.1 Implicit conversion sequences [over.best.ics]

[Contents](../README.md) · up: [12.4.3 Best viable function \[over.match.best\]](over.match.best.md) · prev: [12.4.3 Best viable function \[over.match.best\]](over.match.best.md) · next: [12.4.3.1.1 Standard conversion sequences \[over.ics.scs\]](over.ics.scs.md)

**1** An *implicit conversion sequence* is a sequence of conversions used to convert an argument in a function call to the type of the corresponding parameter of the function being called. The sequence of conversions is an implicit conversion as defined in [[conv]](../expr/conv.md), which means it is governed by the rules for initialization of an object or reference by a single expression ([[dcl.init]](../dcl/dcl.init.md), [[dcl.init.ref]](../dcl/dcl.init.ref.md)).

**2** Implicit conversion sequences are concerned only with the type, cv-qualification, and value category of the argument and how these are converted to match the corresponding properties of the parameter. [*Note 1*: Other properties, such as the lifetime, storage class, alignment, accessibility of the argument, whether the argument is a bit-field, and whether a function is deleted ([[dcl.fct.def.delete]](../dcl/dcl.fct.def.delete.md)), are ignored. So, although an implicit conversion sequence can be defined for a given argument-parameter pair, the conversion from the argument to the parameter might still be ill-formed in the final analysis. — *end note*]

**3** A well-formed implicit conversion sequence is one of the following forms:

- a standard conversion sequence ([[over.ics.scs]](over.ics.scs.md)),
- a user-defined conversion sequence ([[over.ics.user]](over.ics.user.md)), or
- an ellipsis conversion sequence ([[over.ics.ellipsis]](over.ics.ellipsis.md)).

**4** However, if the target is

- the first parameter of a constructor or
- the implicit object parameter of a user-defined conversion function

and the constructor or user-defined conversion function is a candidate by

- [[over.match.ctor]](over.match.ctor.md), when the argument is the temporary in the second step of a class copy-initialization,
- [[over.match.copy]](over.match.copy.md), [[over.match.conv]](over.match.conv.md), or [[over.match.ref]](over.match.ref.md) (in all cases), or
- the second phase of [[over.match.list]](over.match.list.md) when the initializer list has exactly one element that is itself an initializer list, and the target is the first parameter of a constructor of class `X`, and the conversion is to `X` or reference to *cv* `X`,

user-defined conversion sequences are not considered. [*Note 2*: These rules prevent more than one user-defined conversion from being applied during overload resolution, thereby avoiding infinite recursion. — *end note*] [*Example 1*:

```cpp
struct Y { Y(int); };
struct A { operator int(); };
Y y1 = A();         // error: A::operator int() is not a candidate

struct X { X(); };
struct B { operator X(); };
B b;
X x{{b}};           // error: B::operator X() is not a candidate
```

— *end example*]

**5** For the case where the parameter type is a reference, see [[over.ics.ref]](over.ics.ref.md).

**6** When the parameter type is not a reference, the implicit conversion sequence models a copy-initialization of the parameter from the argument expression. The implicit conversion sequence is the one required to convert the argument expression to a prvalue of the type of the parameter. [*Note 3*: When the parameter has a class type, this is a conceptual conversion defined for the purposes of [[over]](over.md); the actual initialization is defined in terms of constructors and is not a conversion. — *end note*] Any difference in top-level cv-qualification is subsumed by the initialization itself and does not constitute a conversion. [*Example 2*: A parameter of type `A` can be initialized from an argument of type `const A`. The implicit conversion sequence for that case is the identity sequence; it contains no “conversion” from `const A` to `A`. — *end example*] When the parameter has a class type and the argument expression has the same type, the implicit conversion sequence is an identity conversion. When the parameter has a class type and the argument expression has a derived class type, the implicit conversion sequence is a derived-to-base conversion from the derived class to the base class. [*Note 4*: There is no such standard conversion; this derived-to-base conversion exists only in the description of implicit conversion sequences. — *end note*] A derived-to-base conversion has Conversion rank ([[over.ics.scs]](over.ics.scs.md)).

**7** In all contexts, when converting to the implicit object parameter or when converting to the left operand of an assignment operation only standard conversion sequences are allowed.

**8** If no conversions are required to match an argument to a parameter type, the implicit conversion sequence is the standard conversion sequence consisting of the identity conversion ([[over.ics.scs]](over.ics.scs.md)).

**9** If no sequence of conversions can be found to convert an argument to a parameter type, an implicit conversion sequence cannot be formed.

**10** If there are multiple well-formed implicit conversion sequences converting the argument to the parameter type, the implicit conversion sequence associated with the parameter is defined to be the unique conversion sequence designated the *ambiguous conversion sequence*. For the purpose of ranking implicit conversion sequences as described in [[over.ics.rank]](over.ics.rank.md), the ambiguous conversion sequence is treated as a user-defined conversion sequence that is indistinguishable from any other user-defined conversion sequence. [*Note 5*: This rule prevents a function from becoming non-viable because of an ambiguous conversion sequence for one of its parameters. [*Example 3*:

```cpp
class B;
class A { A (B&);};
class B { operator A (); };
class C { C (B&); };
void f(A) { }
void f(C) { }
B b;
f(b);               // error: ambiguous because there is a conversion b → C (via constructor)
                    // and an (ambiguous) conversion b → A (via constructor or conversion function)
void f(B) { }
f(b);               // OK, unambiguous
```

— *end example*] — *end note*] If a function that uses the ambiguous conversion sequence is selected as the best viable function, the call will be ill-formed because the conversion of one of the arguments in the call is ambiguous.

**11** The three forms of implicit conversion sequences mentioned above are defined in the following subclauses.

## Subclauses

- [12.4.3.1.1 Standard conversion sequences \[over.ics.scs\]](over.ics.scs.md)
- [12.4.3.1.2 User-defined conversion sequences \[over.ics.user\]](over.ics.user.md)
- [12.4.3.1.3 Ellipsis conversion sequences \[over.ics.ellipsis\]](over.ics.ellipsis.md)
- [12.4.3.1.4 Reference binding \[over.ics.ref\]](over.ics.ref.md)
- [12.4.3.1.5 List-initialization sequence \[over.ics.list\]](over.ics.list.md)
