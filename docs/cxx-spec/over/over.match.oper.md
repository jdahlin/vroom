# 12.4.1.2 Operators in expressions [over.match.oper]

[Contents](../README.md) · up: [12.4.1 Candidate functions and argument lists \[over.match.funcs\]](over.match.funcs.md) · prev: [12.4.1.1.2 Call to object of class type \[over.call.object\]](over.call.object.md) · next: [12.4.1.3 Initialization by constructor \[over.match.ctor\]](over.match.ctor.md)

**1** If no operand of an operator in an expression has a type that is a class or an enumeration, the operator is assumed to be a built-in operator and interpreted according to [[expr.compound]](../expr/expr.compound.md). [*Note 1*: Because `.`, `.*`, and `::` cannot be overloaded, these operators are always built-in operators interpreted according to [[expr.compound]](../expr/expr.compound.md). `?:` cannot be overloaded, but the rules in this subclause are used to determine the conversions to be applied to the second and third operands when they have class or enumeration type ([[expr.cond]](../expr/expr.cond.md)). — *end note*] [*Example 1*:

```cpp
struct String {
  String (const String&);
  String (const char*);
  operator const char* ();
};
String operator + (const String&, const String&);

void f() {
  const char* p= "one" + "two"; // error: cannot add two pointers; overloaded operator+ not considered
                                // because neither operand has class or enumeration type
  int I = 1 + 1;                // always evaluates to 2 even if class or enumeration types exist
                                // that would perform the operation.
}
```

— *end example*]

**2** If either operand has a type that is a class or an enumeration, a user-defined operator function might be declared that implements this operator or a user-defined conversion can be necessary to convert the operand to a type that is appropriate for a built-in operator. In this case, overload resolution is used to determine which operator function or built-in operator is to be invoked to implement the operator. Therefore, the operator notation is first transformed to the equivalent function-call notation as summarized in [Table 15](over.match.oper.md#tab:over.match.oper) (where `@` denotes one of the operators covered in the specified subclause). However, the operands are sequenced in the order prescribed for the built-in operator ([[expr.compound]](../expr/expr.compound.md)).

<a id="tab:over.match.oper"></a>
**Table 15: Relationship between operator and function call notation** [tab:over.match.oper]

| Subclause | Expression | As member function | As non-member function |
| --- | --- | --- | --- |
| [[over.unary]](over.unary.md) | `@a` | `(a).operator@ ()` | `operator@(a)` |
| [[over.binary]](over.binary.md) | `a@b` | `(a).operator@ (b)` | `operator@(a, b)` |
| [[over.ass]](over.ass.md) | `a=b` | `(a).operator= (b)` |  |
| [[over.sub]](over.sub.md) | `a[b]` | `(a).operator[](b)` |  |
| [[over.ref]](over.ref.md) | `a->` | `(a).operator->()` |  |
| [[over.inc]](over.inc.md) | `a@` | `(a).operator@ (0)` | `operator@(a, 0)` |

**3** For a unary operator `@` with an operand of type *cv1* `T1`, and for a binary operator `@` with a left operand of type *cv1* `T1` and a right operand of type *cv2* `T2`, four sets of candidate functions, designated *member candidates*, *non-member candidates*, *built-in candidates*, and *rewritten candidates*, are constructed as follows:

- If `T1` is a complete class type or a class currently being defined, the set of member candidates is the result of the qualified lookup of `T1::operator@` ([[over.call.func]](over.call.func.md)); otherwise, the set of member candidates is empty.

- The set of non-member candidates is the result of the unqualified lookup of `operator@` in the context of the expression according to the usual rules for name lookup in unqualified function calls ([[basic.lookup.argdep]](../basic/basic.lookup.argdep.md)) except that all member functions are ignored. However, if no operand has a class type, only those non-member functions in the lookup set that have a first parameter of type `T1` or “reference to *cv* `T1`”, when `T1` is an enumeration type, or (if there is a right operand) a second parameter of type `T2` or “reference to *cv* `T2`”, when `T2` is an enumeration type, are candidate functions.

- For the operator `,`, the unary operator `&`, or the operator `->`, the built-in candidates set is empty. For all other operators, the built-in candidates include all of the candidate operator functions defined in [[over.built]](over.built.md) that, compared to the given operator,

  - have the same operator name, and
  - accept the same number of operands, and
  - accept operand types to which the given operand or operands can be converted according to [[over.best.ics]](over.best.ics.md), and
  - do not have the same parameter-type-list as any non-member candidate that is not a function template specialization.

- The rewritten candidate set is determined as follows:

  - For the relational ([[expr.rel]](../expr/expr.rel.md)) operators, the rewritten candidates include all non-rewritten candidates for the expression `x <=> y`.
  - For the relational ([[expr.rel]](../expr/expr.rel.md)) and three-way comparison ([[expr.spaceship]](../expr/expr.spaceship.md)) operators, the rewritten candidates also include a synthesized candidate, with the order of the two parameters reversed, for each non-rewritten candidate for the expression `y <=> x`.
  - For the `!=` operator ([[expr.eq]](../expr/expr.eq.md)), the rewritten candidates include all non-rewritten candidates for the expression `x == y`.
  - For the equality operators, the rewritten candidates also include a synthesized candidate, with the order of the two parameters reversed, for each non-rewritten candidate for the expression `y == x`.
  - For all other operators, the rewritten candidate set is empty.

  [*Note 2*: A candidate synthesized from a member candidate has its implicit object parameter as the second parameter, thus implicit conversions are considered for the first, but not for the second, parameter. — *end note*]

**4** For the built-in assignment operators, conversions of the left operand are restricted as follows:

- no temporaries are introduced to hold the left operand, and
- no user-defined conversions are applied to the left operand to achieve a type match with the left-most parameter of a built-in candidate.

**5** For all other operators, no such restrictions apply.

**6** The set of candidate functions for overload resolution for some operator `@` is the union of the member candidates, the non-member candidates, the built-in candidates, and the rewritten candidates for that operator `@`.

**7** The argument list contains all of the operands of the operator. The best function from the set of candidate functions is selected according to [[over.match.viable]](over.match.viable.md) and [[over.match.best]](over.match.best.md).[^1] [*Example 2*:

```cpp
struct A {
  operator int();
};
A operator+(const A&, const A&);
void m() {
  A a, b;
  a + b;                        // operator+(a, b) chosen over int(a) + int(b)
}
```

— *end example*]

**8** If a rewritten `operator<=>` candidate is selected by overload resolution for an operator `@`, `x @ y` is interpreted as `0 @ (y <=> x)` if the selected candidate is a synthesized candidate with reversed order of parameters, or `(x <=> y) @ 0` otherwise, using the selected rewritten `operator<=>` candidate. Rewritten candidates for the operator `@` are not considered in the context of the resulting expression.

**9** If a rewritten `operator==` candidate is selected by overload resolution for an operator `@`, its return type shall be *cv* `bool`, and `x @ y` is interpreted as:

- if `@` is `!=` and the selected candidate is a synthesized candidate with reversed order of parameters, `!(y == x)`,
- otherwise, if `@` is `!=`, `!(x == y)`,
- otherwise (when `@` is `==`), `y == x`,

in each case using the selected rewritten `operator==` candidate.

**10** If a built-in candidate is selected by overload resolution, the operands of class type are converted to the types of the corresponding parameters of the selected operation function, except that the second standard conversion sequence of a user-defined conversion sequence ([[over.ics.user]](over.ics.user.md)) is not applied. Then the operator is treated as the corresponding built-in operator and interpreted according to [[expr.compound]](../expr/expr.compound.md). [*Example 3*:

```cpp
struct X {
  operator double();
};

struct Y {
  operator int*();
};

int *a = Y() + 100.0;           // error: pointer arithmetic requires integral operand
int *b = Y() + X();             // error: pointer arithmetic requires integral operand
```

— *end example*]

**11** The second operand of operator `->` is ignored in selecting an `operator->` function, and is not an argument when the `operator->` function is called. When `operator->` returns, the operator `->` is applied to the value returned, with the original second operand.[^2]

**12** If the operator is the operator `,`, the unary operator `&`, or the operator `->`, and there are no viable functions, then the operator is assumed to be the built-in operator and interpreted according to [[expr.compound]](../expr/expr.compound.md).

**13** [*Note 3*: The lookup rules for operators in expressions are different than the lookup rules for operator function names in a function call, as shown in the following example:

```cpp
struct A { };
void operator + (A, A);

struct B {
  void operator + (B);
  void f ();
};

A a;

void B::f() {
  operator+ (a,a);              // error: global operator hidden by member
  a + a;                        // OK: calls global operator+
}
```

— *end note*]

[^1]: If the set of candidate functions is empty, overload resolution is unsuccessful.
[^2]: If the value returned by the `operator->` function has class type, this may result in selecting and calling another `operator->` function. The process repeats until an `operator->` function returns a value of non-class type.
