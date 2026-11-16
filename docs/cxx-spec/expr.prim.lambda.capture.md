# 7.5.5.2 Captures [expr.prim.lambda.capture]

[Contents](README.md) · up: [7.5.5 Lambda expressions \[expr.prim.lambda\]](expr.prim.lambda.md) · prev: [7.5.5.1 Closure types \[expr.prim.lambda.closure\]](expr.prim.lambda.closure.md) · next: [7.5.6 Fold expressions \[expr.prim.fold\]](expr.prim.fold.md)

```bnf
lambda-capture:
    capture-default
    capture-list
    capture-default ',' capture-list
```

```bnf
capture-default:
    '&'
    '='
```

```bnf
capture-list:
    capture
    capture-list ',' capture
```

```bnf
capture:
    simple-capture
    init-capture
```

```bnf
simple-capture:
    identifier '...'ₒₚₜ
    '&' identifier '...'ₒₚₜ
    'this'
    '*' 'this'
```

```bnf
init-capture:
    '...'ₒₚₜ identifier initializer
    '&' '...'ₒₚₜ identifier initializer
```

**1** The body of a *lambda-expression* may refer to variables with automatic storage duration and the `*this` object (if any) of enclosing block scopes by capturing those entities, as described below.

**2** If a *lambda-capture* includes a *capture-default* that is `&`, no identifier in a *simple-capture* of that *lambda-capture* shall be preceded by `&`. If a *lambda-capture* includes a *capture-default* that is `=`, each *simple-capture* of that *lambda-capture* shall be of the form “`&` *identifier* `...`ₒₚₜ”, “`this`”, or “`* this`”. [*Note 1*: The form `[&,this]` is redundant but accepted for compatibility with ISO C++ 2014. — *end note*] Ignoring appearances in *initializer*s of *init-capture*s, an identifier or `this` shall not appear more than once in a *lambda-capture*. [*Example 1*:

```cpp
struct S2 { void f(int i); };
void S2::f(int i) {
  [&, i]{ };        // OK
  [&, this, i]{ };  // OK, equivalent to [&, i]
  [&, &i]{ };       // error: i preceded by & when & is the default
  [=, *this]{ };    // OK
  [=, this]{ };     // OK, equivalent to [=]
  [i, i]{ };        // error: i repeated
  [this, *this]{ }; // error: this appears twice
}
```

— *end example*]

**3** A *lambda-expression* shall not have a *capture-default* or *simple-capture* in its *lambda-introducer* unless its innermost enclosing scope is a block scope ([[basic.scope.block]](basic.scope.block.md)) or it appears within a default member initializer and its innermost enclosing scope is the corresponding class scope ([[basic.scope.class]](basic.scope.class.md)).

**4** The *identifier* in a *simple-capture* is looked up using the usual rules for unqualified name lookup ([[basic.lookup.unqual]](basic.lookup.unqual.md)); each such lookup shall find a local entity. The *simple-capture*s `this` and `* this` denote the local entity `*this`. An entity that is designated by a *simple-capture* is said to be *explicitly captured*.

**5** If an *identifier* in a *simple-capture* appears as the *declarator-id* of a parameter of the *lambda-declarator*’s *parameter-declaration-clause*, the program is ill-formed. [*Example 2*:

```cpp
void f() {
  int x = 0;
  auto g = [x](int x) { return 0; };    // error: parameter and simple-capture have the same name
}
```

— *end example*]

**6** An *init-capture* without ellipsis behaves as if it declares and explicitly captures a variable of the form “`auto` *init-capture* `;`” whose declarative region is the *lambda-expression*’s *compound-statement*, except that:

- if the capture is by copy (see below), the non-static data member declared for the capture and the variable are treated as two different ways of referring to the same object, which has the lifetime of the non-static data member, and no additional copy and destruction is performed, and
- if the capture is by reference, the variable’s lifetime ends when the closure object’s lifetime ends.

[*Note 2*: This enables an *init-capture* like “`x = std::move(x)`”; the second “`x`” must bind to a declaration in the surrounding context. — *end note*] [*Example 3*:

```cpp
int x = 4;
auto y = [&r = x, x = x+1]()->int {
            r += 2;
            return x+2;
         }();                               // Updates ::x to 6, and initializes y to 7.

auto z = [a = 42](int a) { return 1; };     // error: parameter and local variable have the same name
```

— *end example*]

**7** For the purposes of lambda capture, an expression potentially references local entities as follows:

- An *id-expression* that names a local entity potentially references that entity; an *id-expression* that names one or more non-static class members and does not form a pointer to member ([[expr.unary.op]](expr.unary.op.md)) potentially references `*this`. [*Note 3*: This occurs even if overload resolution selects a static member function for the *id-expression*. — *end note*]
- A `this` expression potentially references `*this`.
- A *lambda-expression* potentially references the local entities named by its *simple-capture*s.

If an expression potentially references a local entity within a declarative region in which it is odr-usable, and the expression would be potentially evaluated if the effect of any enclosing `typeid` expressions ([[expr.typeid]](expr.typeid.md)) were ignored, the entity is said to be *implicitly captured* by each intervening *lambda-expression* with an associated *capture-default* that does not explicitly capture it. The implicit capture of `*this` is deprecated when the *capture-default* is `=`; see [[depr.capture.this]](depr.capture.this.md). [*Example 4*:

```cpp
void f(int, const int (&)[2] = {});         // #1
void f(const int&, const int (&)[1]);       // #2
void test() {
  const int x = 17;
  auto g = [](auto a) {
    f(x);                       // OK: calls #1, does not capture x
  };

  auto g1 = [=](auto a) {
    f(x);                       // OK: calls #1, captures x
  };

  auto g2 = [=](auto a) {
    int selector[sizeof(a) == 1 ? 1 : 2]{};
    f(x, selector);             // OK: captures x, might call #1 or #2
  };

  auto g3 = [=](auto a) {
    typeid(a + x);              // captures x regardless of whether a + x is an unevaluated operand
  };
}
```

Within `g1`, an implementation might optimize away the capture of `x` as it is not odr-used. — *end example*] [*Note 4*: The set of captured entities is determined syntactically, and entities might be implicitly captured even if the expression denoting a local entity is within a discarded statement ([[stmt.if]](stmt.if.md)). [*Example 5*:

```cpp
template<bool B>
void f(int n) {
  [=](auto a) {
    if constexpr (B && sizeof(a) > 4) {
      (void)n;                  // captures n regardless of the value of B and sizeof(int)
    }
  }(0);
}
```

— *end example*] — *end note*]

**8** An entity is *captured* if it is captured explicitly or implicitly. An entity captured by a *lambda-expression* is odr-used ([[basic.def.odr]](basic.def.odr.md)) in the scope containing the *lambda-expression*. [*Note 5*: As a consequence, if a *lambda-expression* explicitly captures an entity that is not odr-usable, the program is ill-formed ([[basic.def.odr]](basic.def.odr.md)). — *end note*] [*Example 6*:

```cpp
void f1(int i) {
  int const N = 20;
  auto m1 = [=]{
    int const M = 30;
    auto m2 = [i]{
      int x[N][M];              // OK: N and M are not odr-used
      x[0][0] = i;              // OK: i is explicitly captured by m2 and implicitly captured by m1
    };
  };
  struct s1 {
    int f;
    void work(int n) {
      int m = n*n;
      int j = 40;
      auto m3 = [this,m] {
        auto m4 = [&,j] {       // error: j not odr-usable due to intervening lambda m3
          int x = n;            // error: n is odr-used but not odr-usable due to intervening lambda m3
          x += m;               // OK: m implicitly captured by m4 and explicitly captured by m3
          x += i;               // error: i is odr-used but not odr-usable
                                // due to intervening function and class scopes
          x += f;               // OK: this captured implicitly by m4 and explicitly by m3
        };
      };
    }
  };
}

struct s2 {
  double ohseven = .007;
  auto f() {
    return [this] {
      return [*this] {
          return ohseven;       // OK
      };
    }();
  }
  auto g() {
    return [] {
      return [*this] { };       // error: *this not captured by outer lambda-expression
    }();
  }
};
```

— *end example*]

**9** [*Note 6*: Because local entities are not odr-usable within a default argument ([[basic.def.odr]](basic.def.odr.md)), a *lambda-expression* appearing in a default argument cannot implicitly or explicitly capture any local entity. Such a *lambda-expression* can still have an *init-capture* if any full-expression in its *initializer* satisfies the constraints of an expression appearing in a default argument ([[dcl.fct.default]](dcl.fct.default.md)). — *end note*] [*Example 7*:

```cpp
void f2() {
  int i = 1;
  void g1(int = ([i]{ return i; })());          // error
  void g2(int = ([i]{ return 0; })());          // error
  void g3(int = ([=]{ return i; })());          // error
  void g4(int = ([=]{ return 0; })());          // OK
  void g5(int = ([]{ return sizeof i; })());    // OK
  void g6(int = ([x=1]{ return x; })());        // OK
  void g7(int = ([x=i]{ return x; })());        // error
}
```

— *end example*]

**10** An entity is *captured by copy* if

- it is implicitly captured, the *capture-default* is `=`, and the captured entity is not `*this`, or
- it is explicitly captured with a capture that is not of the form `this`, `&` *identifier*, or `&` *identifier* *initializer*.

For each entity captured by copy, an unnamed non-static data member is declared in the closure type. The declaration order of these members is unspecified. The type of such a data member is the referenced type if the entity is a reference to an object, an lvalue reference to the referenced function type if the entity is a reference to a function, or the type of the corresponding captured entity otherwise. A member of an anonymous union shall not be captured by copy.

**11** Every *id-expression* within the *compound-statement* of a *lambda-expression* that is an odr-use ([[basic.def.odr]](basic.def.odr.md)) of an entity captured by copy is transformed into an access to the corresponding unnamed data member of the closure type. [*Note 7*: An *id-expression* that is not an odr-use refers to the original entity, never to a member of the closure type. However, such an *id-expression* can still cause the implicit capture of the entity. — *end note*] If `*this` is captured by copy, each expression that odr-uses `*this` is transformed to instead refer to the corresponding unnamed data member of the closure type. [*Example 8*:

```cpp
void f(const int*);
void g() {
  const int N = 10;
  [=] {
    int arr[N];     // OK: not an odr-use, refers to automatic variable
    f(&N);          // OK: causes N to be captured; &N points to
                    // the corresponding member of the closure type
  };
}
```

— *end example*]

**12** An entity is *captured by reference* if it is implicitly or explicitly captured but not captured by copy. It is unspecified whether additional unnamed non-static data members are declared in the closure type for entities captured by reference. If declared, such non-static data members shall be of literal type. [*Example 9*:

```cpp
// The inner closure type must be a literal type regardless of how reference captures are represented.
static_assert([](int n) { return [&n] { return ++n; }(); }(3) == 4);
```

— *end example*] A bit-field or a member of an anonymous union shall not be captured by reference.

**13** An *id-expression* within the *compound-statement* of a *lambda-expression* that is an odr-use of a reference captured by reference refers to the entity to which the captured reference is bound and not to the captured reference. [*Note 8*: The validity of such captures is determined by the lifetime of the object to which the reference refers, not by the lifetime of the reference itself. — *end note*] [*Example 10*:

```cpp
auto h(int &r) {
  return [&] {
    ++r;            // Valid after h returns if the lifetime of the
                    // object to which r is bound has not ended
  };
}
```

— *end example*]

**14** If a *lambda-expression* `m2` captures an entity and that entity is captured by an immediately enclosing *lambda-expression* `m1`, then `m2`’s capture is transformed as follows:

- if `m1` captures the entity by copy, `m2` captures the corresponding non-static data member of `m1`’s closure type;
- if `m1` captures the entity by reference, `m2` captures the same entity captured by `m1`.

[*Example 11*: The nested *lambda-expression*s and invocations below will output `123234`.

```cpp
int a = 1, b = 1, c = 1;
auto m1 = [a, &b, &c]() mutable {
  auto m2 = [a, b, &c]() mutable {
    std::cout << a << b << c;
    a = 4; b = 4; c = 4;
  };
  a = 3; b = 3; c = 3;
  m2();
};
a = 2; b = 2; c = 2;
m1();
std::cout << a << b << c;
```

— *end example*]

**15** When the *lambda-expression* is evaluated, the entities that are captured by copy are used to direct-initialize each corresponding non-static data member of the resulting closure object, and the non-static data members corresponding to the *init-capture*s are initialized as indicated by the corresponding *initializer* (which may be copy- or direct-initialization). (For array members, the array elements are direct-initialized in increasing subscript order.) These initializations are performed in the (unspecified) order in which the non-static data members are declared. [*Note 9*: This ensures that the destructions will occur in the reverse order of the constructions. — *end note*]

**16** [*Note 10*: If a non-reference entity is implicitly or explicitly captured by reference, invoking the function call operator of the corresponding *lambda-expression* after the lifetime of the entity has ended is likely to result in undefined behavior. — *end note*]

**17** A *simple-capture* containing an ellipsis is a pack expansion ([[temp.variadic]](temp.variadic.md)). An *init-capture* containing an ellipsis is a pack expansion that introduces an *init-capture* pack ([[temp.variadic]](temp.variadic.md)) whose declarative region is the *lambda-expression*’s *compound-statement*. [*Example 12*:

```cpp
template<class... Args>
void f(Args... args) {
  auto lm = [&, args...] { return g(args...); };
  lm();

  auto lm2 = [...xs=std::move(args)] { return g(xs...); };
  lm2();
}
```

— *end example*]
