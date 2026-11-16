# 9.4 Initializers [dcl.init]

[Contents](../README.md) · up: [9 Declarations \[dcl.dcl\]](dcl.dcl.md) · prev: [9.3.3.6 Default arguments \[dcl.fct.default\]](dcl.fct.default.md) · next: [9.4.1 Aggregates \[dcl.init.aggr\]](dcl.init.aggr.md)

**1** The process of initialization described in this subclause applies to all initializations regardless of syntactic context, including the initialization of a function parameter ([[expr.call]](../expr/expr.call.md)), the initialization of a return value ([[stmt.return]](../stmt/stmt.return.md)), or when an initializer follows a declarator.

```bnf
initializer:
    brace-or-equal-initializer
    '(' expression-list ')'
```

```bnf
brace-or-equal-initializer:
    '=' initializer-clause
    braced-init-list
```

```bnf
initializer-clause:
    assignment-expression
    braced-init-list
```

```bnf
braced-init-list:
    '{' initializer-list ','ₒₚₜ '}'
    '{' designated-initializer-list ','ₒₚₜ '}'
    '{' '}'
```

```bnf
initializer-list:
    initializer-clause '...'ₒₚₜ
    initializer-list ',' initializer-clause '...'ₒₚₜ
```

```bnf
designated-initializer-list:
    designated-initializer-clause
    designated-initializer-list ',' designated-initializer-clause
```

```bnf
designated-initializer-clause:
    designator brace-or-equal-initializer
```

```bnf
designator:
    '.' identifier
```

```bnf
expr-or-braced-init-list:
    expression
    braced-init-list
```

[*Note 1*: The rules in this subclause apply even if the grammar permits only the *brace-or-equal-initializer* form of *initializer* in a given context. — *end note*]

**2** Except for objects declared with the `constexpr` specifier, for which see [[dcl.constexpr]](dcl.constexpr.md), an *initializer* in the definition of a variable can consist of arbitrary expressions involving literals and previously declared variables and functions, regardless of the variable’s storage duration. [*Example 1*:

```cpp
int f(int);
int a = 2;
int b = f(a);
int c(b);
```

— *end example*]

**3** [*Note 2*: Default arguments are more restricted; see [[dcl.fct.default]](dcl.fct.default.md). — *end note*]

**4** [*Note 3*: The order of initialization of variables with static storage duration is described in [[basic.start]](../basic/basic.start.md) and [[stmt.dcl]](../stmt/stmt.dcl.md). — *end note*]

**5** A declaration of a block-scope variable with external or internal linkage that has an *initializer* is ill-formed.

**6** To *zero-initialize* an object or reference of type `T` means:

- if `T` is a scalar type ([[basic.types]](../basic/basic.types.md)), the object is initialized to the value obtained by converting the integer literal `0` (zero) to `T`;[^1]
- if `T` is a (possibly cv-qualified) non-union class type, its padding bits ([[basic.types]](../basic/basic.types.md)) are initialized to zero bits and each non-static data member, each non-virtual base class subobject, and, if the object is not a base class subobject, each virtual base class subobject is zero-initialized;
- if `T` is a (possibly cv-qualified) union type, its padding bits ([[basic.types]](../basic/basic.types.md)) are initialized to zero bits and the object’s first non-static named data member is zero-initialized;
- if `T` is an array type, each element is zero-initialized;
- if `T` is a reference type, no initialization is performed.

**7** To *default-initialize* an object of type `T` means:

- If `T` is a (possibly cv-qualified) class type ([[class]](../class/class.md)), constructors are considered. The applicable constructors are enumerated ([[over.match.ctor]](../over/over.match.ctor.md)), and the best one for the *initializer* `()` is chosen through overload resolution ([[over.match]](../over/over.match.md)). The constructor thus selected is called, with an empty argument list, to initialize the object.
- If `T` is an array type, each element is default-initialized.
- Otherwise, no initialization is performed.

A class type `T` is *const-default-constructible* if default-initialization of `T` would invoke a user-provided constructor of `T` (not inherited from a base class) or if

- each direct non-variant non-static data member `M` of `T` has a default member initializer or, if `M` is of class type `X` (or array thereof), `X` is const-default-constructible,
- if `T` is a union with at least one non-static data member, exactly one variant member has a default member initializer,
- if `T` is not a union, for each anonymous union member with at least one non-static data member (if any), exactly one non-static data member has a default member initializer, and
- each potentially constructed base class of `T` is const-default-constructible.

If a program calls for the default-initialization of an object of a const-qualified type `T`, `T` shall be a const-default-constructible class type or array thereof.

**8** To *value-initialize* an object of type `T` means:

- if `T` is a (possibly cv-qualified) class type ([[class]](../class/class.md)), then

  - if `T` has either no default constructor ([[class.default.ctor]](../class/class.default.ctor.md)) or a default constructor that is user-provided or deleted, then the object is default-initialized;
  - otherwise, the object is zero-initialized and the semantic constraints for default-initialization are checked, and if `T` has a non-trivial default constructor, the object is default-initialized;

- if `T` is an array type, then each element is value-initialized;

- otherwise, the object is zero-initialized.

**9** A program that calls for default-initialization or value-initialization of an entity of reference type is ill-formed.

**10** [*Note 4*: For every object of static storage duration, static initialization ([[basic.start.static]](../basic/basic.start.static.md)) is performed at program startup before any other initialization takes place. In some cases, additional initialization is done later. — *end note*]

**11** An object whose initializer is an empty set of parentheses, i.e., `()`, shall be value-initialized.

[*Note 5*: Since `()` is not permitted by the syntax for *initializer*,

```cpp
X a();
```

is not the declaration of an object of class `X`, but the declaration of a function taking no argument and returning an `X`. The form `()` is permitted in certain other initialization contexts ([[expr.new]](../expr/expr.new.md), [[expr.type.conv]](../expr/expr.type.conv.md), [[class.base.init]](../class/class.base.init.md)). — *end note*]

**12** If no initializer is specified for an object, the object is default-initialized.

**13** An initializer for a static member is in the scope of the member’s class. [*Example 2*:

```cpp
int a;

struct X {
  static int a;
  static int b;
};

int X::a = 1;
int X::b = a;                   // X::b = X::a
```

— *end example*]

**14** If the entity being initialized does not have class type, the *expression-list* in a parenthesized initializer shall be a single expression.

**15** The initialization that occurs in the `=` form of a *brace-or-equal-initializer* or *condition* ([[stmt.select]](../stmt/stmt.select.md)), as well as in argument passing, function return, throwing an exception ([[except.throw]](../except/except.throw.md)), handling an exception ([[except.handle]](../except/except.handle.md)), and aggregate member initialization ([[dcl.init.aggr]](dcl.init.aggr.md)), is called *copy-initialization*. [*Note 6*: Copy-initialization may invoke a move ([[class.copy.ctor]](../class/class.copy.ctor.md)). — *end note*]

**16** The initialization that occurs

- for an *initializer* that is a parenthesized *expression-list* or a *braced-init-list*,
- for a *new-initializer* ([[expr.new]](../expr/expr.new.md)),
- in a `static_cast` expression ([[expr.static.cast]](../expr/expr.static.cast.md)),
- in a functional notation type conversion ([[expr.type.conv]](../expr/expr.type.conv.md)), and
- in the *braced-init-list* form of a *condition*

is called *direct-initialization*.

**17** The semantics of initializers are as follows. The *destination type* is the type of the object or reference being initialized and the *source type* is the type of the initializer expression. If the initializer is not a single (possibly parenthesized) expression, the source type is not defined.

- If the initializer is a (non-parenthesized) *braced-init-list* or is `=` *braced-init-list*, the object or reference is list-initialized ([[dcl.init.list]](dcl.init.list.md)).

- If the destination type is a reference type, see [[dcl.init.ref]](dcl.init.ref.md).

- If the destination type is an array of characters, an array of `char8_t`, an array of `char16_t`, an array of `char32_t`, or an array of `wchar_t`, and the initializer is a *string-literal*, see [[dcl.init.string]](dcl.init.string.md).

- If the initializer is `()`, the object is value-initialized.

- Otherwise, if the destination type is an array, the object is initialized as follows. Let *x*<sub>1</sub>, …, *x*<sub>*k*</sub> be the elements of the *expression-list*. If the destination type is an array of unknown bound, it is defined as having *k* elements. Let *n* denote the array size after this potential adjustment. If *k* is greater than *n*, the program is ill-formed. Otherwise, the *i*<sup></sup>*th* array element is copy-initialized with *x*<sub>*i*</sub> for each 1 ≤ *i* ≤ *k*, and value-initialized for each *k* &lt; *i* ≤ *n*. For each 1 ≤ *i* &lt; *j* ≤ *n*, every value computation and side effect associated with the initialization of the *i*<sup></sup>*th* element of the array is sequenced before those associated with the initialization of the *j*<sup></sup>*th* element.

- Otherwise, if the destination type is a (possibly cv-qualified) class type:

  - If the initializer expression is a prvalue and the cv-unqualified version of the source type is the same class as the class of the destination, the initializer expression is used to initialize the destination object. [*Example 3*: `T x = T(T(T()));` calls the `T` default constructor to initialize `x`. — *end example*]

  - Otherwise, if the initialization is direct-initialization, or if it is copy-initialization where the cv-unqualified version of the source type is the same class as, or a derived class of, the class of the destination, constructors are considered. The applicable constructors are enumerated ([[over.match.ctor]](../over/over.match.ctor.md)), and the best one is chosen through overload resolution ([[over.match]](../over/over.match.md)). Then:

    - If overload resolution is successful, the selected constructor is called to initialize the object, with the initializer expression or *expression-list* as its argument(s).

    - Otherwise, if no constructor is viable, the destination type is an aggregate class, and the initializer is a parenthesized *expression-list*, the object is initialized as follows. Let *e*<sub>1</sub>, …, *e*<sub>*n*</sub> be the elements of the aggregate ([[dcl.init.aggr]](dcl.init.aggr.md)). Let *x*<sub>1</sub>, …, *x*<sub>*k*</sub> be the elements of the *expression-list*. If *k* is greater than *n*, the program is ill-formed. The element *e*<sub>*i*</sub> is copy-initialized with *x*<sub>*i*</sub> for 1 ≤ *i* ≤ *k*. The remaining elements are initialized with their default member initializers, if any, and otherwise are value-initialized. For each 1 ≤ *i* &lt; *j* ≤ *n*, every value computation and side effect associated with the initialization of *e*<sub>*i*</sub> is sequenced before those associated with the initialization of *e*<sub>*j*</sub>. [*Note 7*: By contrast with direct-list-initialization, narrowing conversions ([[dcl.init.list]](dcl.init.list.md)) are permitted, designators are not permitted, a temporary object bound to a reference does not have its lifetime extended ([[class.temporary]](../basic/class.temporary.md)), and there is no brace elision. [*Example 4*:

      ```cpp
      struct A {
        int a;
        int&& r;
      };

      int f();
      int n = 10;

      A a1{1, f()};                   // OK, lifetime is extended
      A a2(1, f());                   // well-formed, but dangling reference
      A a3{1.0, 1};                   // error: narrowing conversion
      A a4(1.0, 1);                   // well-formed, but dangling reference
      A a5(1.0, std::move(n));        // OK
      ```

      — *end example*] — *end note*]

    - Otherwise, the initialization is ill-formed.

  - Otherwise (i.e., for the remaining copy-initialization cases), user-defined conversions that can convert from the source type to the destination type or (when a conversion function is used) to a derived class thereof are enumerated as described in [[over.match.copy]](../over/over.match.copy.md), and the best one is chosen through overload resolution ([[over.match]](../over/over.match.md)). If the conversion cannot be done or is ambiguous, the initialization is ill-formed. The function selected is called with the initializer expression as its argument; if the function is a constructor, the call is a prvalue of the cv-unqualified version of the destination type whose result object is initialized by the constructor. The call is used to direct-initialize, according to the rules above, the object that is the destination of the copy-initialization.

- Otherwise, if the source type is a (possibly cv-qualified) class type, conversion functions are considered. The applicable conversion functions are enumerated ([[over.match.conv]](../over/over.match.conv.md)), and the best one is chosen through overload resolution ([[over.match]](../over/over.match.md)). The user-defined conversion so selected is called to convert the initializer expression into the object being initialized. If the conversion cannot be done or is ambiguous, the initialization is ill-formed.

- Otherwise, if the initialization is direct-initialization, the source type is `std::nullptr_t`, and the destination type is `bool`, the initial value of the object being initialized is `false`.

- Otherwise, the initial value of the object being initialized is the (possibly converted) value of the initializer expression. A standard conversion sequence ([[conv]](../expr/conv.md)) will be used, if necessary, to convert the initializer expression to the cv-unqualified version of the destination type; no user-defined conversions are considered. If the conversion cannot be done, the initialization is ill-formed. When initializing a bit-field with a value that it cannot represent, the resulting value of the bit-field is implementation-defined. [*Note 8*: An expression of type “*cv1* `T`” can initialize an object of type “*cv2* `T`” independently of the cv-qualifiers *cv1* and *cv2*.

  ```cpp
  int a;
  const int b = a;
  int c = b;
  ```

  — *end note*]

**18** An *initializer-clause* followed by an ellipsis is a pack expansion ([[temp.variadic]](../temp/temp.variadic.md)).

**19** If the initializer is a parenthesized *expression-list*, the expressions are evaluated in the order specified for function calls ([[expr.call]](../expr/expr.call.md)).

**20** The same *identifier* shall not appear in multiple *designator*s of a *designated-initializer-list*.

**21** An object whose initialization has completed is deemed to be constructed, even if the object is of non-class type or no constructor of the object’s class is invoked for the initialization. [*Note 9*: Such an object might have been value-initialized or initialized by aggregate initialization ([[dcl.init.aggr]](dcl.init.aggr.md)) or by an inherited constructor ([[class.inhctor.init]](../class/class.inhctor.init.md)). — *end note*] Destroying an object of class type invokes the destructor of the class. Destroying a scalar type has no effect other than ending the lifetime of the object ([[basic.life]](../basic/basic.life.md)). Destroying an array destroys each element in reverse subscript order.

**22** A declaration that specifies the initialization of a variable, whether from an explicit initializer or by default-initialization, is called the *initializing declaration* of that variable. [*Note 10*: In most cases this is the defining declaration ([[basic.def]](../basic/basic.def.md)) of the variable, but the initializing declaration of a non-inline static data member ([[class.static.data]](../class/class.static.data.md)) might be the declaration within the class definition and not the definition at namespace scope. — *end note*]

## Subclauses

- [9.4.1 Aggregates \[dcl.init.aggr\]](dcl.init.aggr.md)
- [9.4.2 Character arrays \[dcl.init.string\]](dcl.init.string.md)
- [9.4.3 References \[dcl.init.ref\]](dcl.init.ref.md)
- [9.4.4 List-initialization \[dcl.init.list\]](dcl.init.list.md)

[^1]: As specified in [[conv.ptr]](../expr/conv.ptr.md), converting an integer literal whose value is `0` to a pointer type results in a null pointer value.
