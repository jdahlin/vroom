# 9.4.3 References [dcl.init.ref]

[Contents](README.md) · up: [9.4 Initializers \[dcl.init\]](dcl.init.md) · prev: [9.4.2 Character arrays \[dcl.init.string\]](dcl.init.string.md) · next: [9.4.4 List-initialization \[dcl.init.list\]](dcl.init.list.md)

**1** A variable whose declared type is “reference to type `T`” ([[dcl.ref]](dcl.ref.md)) shall be initialized. [*Example 1*:

```cpp
int g(int) noexcept;
void f() {
  int i;
  int& r = i;                   // r refers to i
  r = 1;                        // the value of i becomes 1
  int* p = &r;                  // p points to i
  int& rr = r;                  // rr refers to what r refers to, that is, to i
  int (&rg)(int) = g;           // rg refers to the function g
  rg(i);                        // calls function g
  int a[3];
  int (&ra)[3] = a;             // ra refers to the array a
  ra[1] = i;                    // modifies a[1]
}
```

— *end example*]

**2** A reference cannot be changed to refer to another object after initialization. [*Note 1*: Assignment to a reference assigns to the object referred to by the reference ([[expr.ass]](expr.ass.md)). — *end note*] Argument passing ([[expr.call]](expr.call.md)) and function value return ([[stmt.return]](stmt.return.md)) are initializations.

**3** The initializer can be omitted for a reference only in a parameter declaration ([[dcl.fct]](dcl.fct.md)), in the declaration of a function return type, in the declaration of a class member within its class definition ([[class.mem]](class.mem.md)), and where the `extern` specifier is explicitly used. [*Example 2*:

```cpp
int& r1;                        // error: initializer missing
extern int& r2;                 // OK
```

— *end example*]

**4** Given types “*cv1* `T1`” and “*cv2* `T2`”, “*cv1* `T1`” is *reference-related* to “*cv2* `T2`” if `T1` is similar ([[conv.qual]](conv.qual.md)) to `T2`, or `T1` is a base class of `T2`. “*cv1* `T1`” is *reference-compatible* with “*cv2* `T2`” if a prvalue of type “pointer to *cv2* `T2`” can be converted to the type “pointer to *cv1* `T1`” via a standard conversion sequence ([[conv]](conv.md)). In all cases where the reference-compatible relationship of two types is used to establish the validity of a reference binding and the standard conversion sequence would be ill-formed, a program that necessitates such a binding is ill-formed.

**5** A reference to type “*cv1* `T1`” is initialized by an expression of type “*cv2* `T2`” as follows:

- If the reference is an lvalue reference and the initializer expression

  - is an lvalue (but is not a bit-field), and “*cv1* `T1`” is reference-compatible with “*cv2* `T2`”, or
  - has a class type (i.e., `T2` is a class type), where `T1` is not reference-related to `T2`, and can be converted to an lvalue of type “*cv3* `T3`”, where “*cv1* `T1`” is reference-compatible with “*cv3* `T3`”[^1] (this conversion is selected by enumerating the applicable conversion functions ([[over.match.ref]](over.match.ref.md)) and choosing the best one through overload resolution ([[over.match]](over.match.md))),

  then the reference is bound to the initializer expression lvalue in the first case and to the lvalue result of the conversion in the second case (or, in either case, to the appropriate base class subobject of the object). [*Note 2*: The usual lvalue-to-rvalue ([[conv.lval]](conv.lval.md)), array-to-pointer ([[conv.array]](conv.array.md)), and function-to-pointer ([[conv.func]](conv.func.md)) standard conversions are not needed, and therefore are suppressed, when such direct bindings to lvalues are done. — *end note*]

  [*Example 3*:

  ```cpp
  double d = 2.0;
  double& rd = d;                 // rd refers to d
  const double& rcd = d;          // rcd refers to d

  struct A { };
  struct B : A { operator int&(); } b;
  A& ra = b;                      // ra refers to A subobject in b
  const A& rca = b;               // rca refers to A subobject in b
  int& ir = B();                  // ir refers to the result of B::operator int&
  ```

  — *end example*]

- Otherwise, if the reference is an lvalue reference to a type that is not const-qualified or is volatile-qualified, the program is ill-formed. [*Example 4*:

  ```cpp
  double& rd2 = 2.0;              // error: not an lvalue and reference not const
  int  i = 2;
  double& rd3 = i;                // error: type mismatch and reference not const
  ```

  — *end example*]

- Otherwise, if the initializer expression

  - is an rvalue (but not a bit-field) or function lvalue and “*cv1* `T1`” is reference-compatible with “*cv2* `T2`”, or
  - has a class type (i.e., `T2` is a class type), where `T1` is not reference-related to `T2`, and can be converted to an rvalue or function lvalue of type “*cv3* `T3`”, where “*cv1* `T1`” is reference-compatible with “*cv3* `T3`” (see [[over.match.ref]](over.match.ref.md)),

  then the value of the initializer expression in the first case and the result of the conversion in the second case is called the converted initializer. If the converted initializer is a prvalue, its type `T4` is adjusted to type “*cv1* `T4`” ([[conv.qual]](conv.qual.md)) and the temporary materialization conversion ([[conv.rval]](conv.rval.md)) is applied. In any case, the reference is bound to the resulting glvalue (or to an appropriate base class subobject).

  [*Example 5*:

  ```cpp
  struct A { };
  struct B : A { } b;
  extern B f();
  const A& rca2 = f();                // bound to the A subobject of the B rvalue.
  A&& rra = f();                      // same as above
  struct X {
    operator B();
    operator int&();
  } x;
  const A& r = x;                     // bound to the A subobject of the result of the conversion
  int i2 = 42;
  int&& rri = static_cast<int&&>(i2); // bound directly to i2
  B&& rrb = x;                        // bound directly to the result of operator B
  ```

  — *end example*]

- Otherwise:

  - If `T1` or `T2` is a class type and `T1` is not reference-related to `T2`, user-defined conversions are considered using the rules for copy-initialization of an object of type “*cv1* `T1`” by user-defined conversion ([[dcl.init]](dcl.init.md), [[over.match.copy]](over.match.copy.md), [[over.match.conv]](over.match.conv.md)); the program is ill-formed if the corresponding non-reference copy-initialization would be ill-formed. The result of the call to the conversion function, as described for the non-reference copy-initialization, is then used to direct-initialize the reference. For this direct-initialization, user-defined conversions are not considered.
  - Otherwise, the initializer expression is implicitly converted to a prvalue of type “*cv1* `T1`”. The temporary materialization conversion is applied and the reference is bound to the result.

  If `T1` is reference-related to `T2`:

  - *cv1* shall be the same cv-qualification as, or greater cv-qualification than, *cv2*; and
  - if the reference is an rvalue reference, the initializer expression shall not be an lvalue.

  [*Example 6*:

  ```cpp
  struct Banana { };
  struct Enigma { operator const Banana(); };
  struct Alaska { operator Banana&(); };
  void enigmatic() {
    typedef const Banana ConstBanana;
    Banana &&banana1 = ConstBanana(); // error
    Banana &&banana2 = Enigma();      // error
    Banana &&banana3 = Alaska();      // error
  }

  const double& rcd2 = 2;             // rcd2 refers to temporary with value 2.0
  double&& rrd = 2;                   // rrd refers to temporary with value 2.0
  const volatile int cvi = 1;
  const int& r2 = cvi;                // error: cv-qualifier dropped
  struct A { operator volatile int&(); } a;
  const int& r3 = a;                  // error: cv-qualifier dropped
                                      // from result of conversion function
  double d2 = 1.0;
  double&& rrd2 = d2;                 // error: initializer is lvalue of related type
  struct X { operator int&(); };
  int&& rri2 = X();                   // error: result of conversion function is lvalue of related type
  int i3 = 2;
  double&& rrd3 = i3;                 // rrd3 refers to temporary with value 2.0
  ```

  — *end example*]

In all cases except the last (i.e., implicitly converting the initializer expression to the referenced type), the reference is said to *bind directly* to the initializer expression.

**6** [*Note 3*: [[class.temporary]](class.temporary.md) describes the lifetime of temporaries bound to references. — *end note*]

[^1]: This requires a conversion function ([[class.conv.fct]](class.conv.fct.md)) returning a reference type.
