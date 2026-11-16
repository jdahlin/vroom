# 7.7 Constant expressions [expr.const]

[Contents](../README.md) · up: [7 Expressions \[expr\]](expr.md) · prev: [7.6.20 Comma operator \[expr.comma\]](expr.comma.md) · next: [8 Statements \[stmt.stmt\]](../stmt/stmt.stmt.md)

**1** Certain contexts require expressions that satisfy additional requirements as detailed in this subclause; other contexts have different semantics depending on whether or not an expression satisfies these requirements. Expressions that satisfy these requirements, assuming that copy elision ([[class.copy.elision]](../class/class.copy.elision.md)) is not performed, are called *constant expressions*. [*Note 1*: Constant expressions can be evaluated during translation. — *end note*]

```bnf
constant-expression:
    conditional-expression
```

**2** A variable or temporary object `o` is *constant-initialized* if

- either it has an initializer or its default-initialization results in some initialization being performed, and
- the full-expression of its initialization is a constant expression when interpreted as a *constant-expression*, except that if `o` is an object, that full-expression may also invoke constexpr constructors for `o` and its subobjects even if those objects are of non-literal class types. [*Note 2*: Such a class may have a non-trivial destructor. Within this evaluation, `std::is_constant_evaluated()` ([[meta.const.eval]](../lib/utilities/meta.const.eval.md)) returns `true`. — *end note*]

**3** A variable is *potentially-constant* if it is constexpr or it has reference or const-qualified integral or enumeration type.

**4** A constant-initialized potentially-constant variable is *usable in constant expressions* at a point *P* if its initializing declaration *D* is reachable from *P* and

- it is constexpr,
- it is not initialized to a TU-local value, or
- *P* is in the same translation unit as *D*.

An object or reference is *usable in constant expressions* if it is

- a variable that is usable in constant expressions, or
- a template parameter object ([[temp.param]](../temp/temp.param.md)), or
- a string literal object ([[lex.string]](../lex/lex.string.md)), or
- a temporary object of non-volatile const-qualified literal type whose lifetime is extended ([[class.temporary]](../basic/class.temporary.md)) to that of a variable that is usable in constant expressions, or
- a non-mutable subobject or reference member of any of the above.

**5** An expression *E* is a *core constant expression* unless the evaluation of *E*, following the rules of the abstract machine ([[intro.execution]](../basic/intro.execution.md)), would evaluate one of the following:

- `this` ([[expr.prim.this]](expr.prim.this.md)), except in a constexpr function ([[dcl.constexpr]](../dcl/dcl.constexpr.md)) that is being evaluated as part of *E*;

- an invocation of a non-constexpr function [*Note 3*: Overload resolution ([[over.match]](../over/over.match.md)) is applied as usual. — *end note*];

- an invocation of an undefined constexpr function;

- an invocation of an instantiated constexpr function that fails to satisfy the requirements for a constexpr function;

- an invocation of a virtual function ([[class.virtual]](../class/class.virtual.md)) for an object unless

  - the object is usable in constant expressions or
  - its lifetime began within the evaluation of *E*;

- an expression that would exceed the implementation-defined limits (see [[implimits]](../implimits/implimits.md));

- an operation that would have undefined behavior as specified in [[intro]](../intro/intro.md) through [[cpp]](../cpp/cpp.md) of this document [*Note 4*: including, for example, signed integer overflow ([[expr.prop]](expr.prop.md)), certain pointer arithmetic ([[expr.add]](expr.add.md)), division by zero ([[expr.mul]](expr.mul.md)), or certain shift operations ([[expr.shift]](expr.shift.md)) — *end note*];

- an lvalue-to-rvalue conversion ([[conv.lval]](conv.lval.md)) unless it is applied to

  - a non-volatile glvalue that refers to an object that is usable in constant expressions, or
  - a non-volatile glvalue of literal type that refers to a non-volatile object whose lifetime began within the evaluation of *E*;

- an lvalue-to-rvalue conversion ([[conv.lval]](conv.lval.md)) that is applied to a glvalue that refers to a non-active member of a union or a subobject thereof;

- an lvalue-to-rvalue conversion that is applied to an object with an indeterminate value ([[basic.indet]](../basic/basic.indet.md));

- an invocation of an implicitly-defined copy/move constructor or copy/move assignment operator for a union whose active member (if any) is mutable, unless the lifetime of the union object began within the evaluation of *E*;

- an *id-expression* that refers to a variable or data member of reference type unless the reference has a preceding initialization and either

  - it is usable in constant expressions or
  - its lifetime began within the evaluation of *E*;

- in a *lambda-expression*, a reference to `this` or to a variable with automatic storage duration defined outside that *lambda-expression*, where the reference would be an odr-use ([[basic.def.odr]](../basic/basic.def.odr.md), [[expr.prim.lambda]](expr.prim.lambda.md)); [*Example 1*:

  ```cpp
  void g() {
    const int n = 0;
    [=] {
      constexpr int i = n;        // OK, n is not odr-used here
      constexpr int j = *&n;      // error: &n would be an odr-use of n
    };
  }
  ```

  — *end example*] [*Note 5*: If the odr-use occurs in an invocation of a function call operator of a closure type, it no longer refers to `this` or to an enclosing automatic variable due to the transformation ([[expr.prim.lambda.capture]](expr.prim.lambda.capture.md)) of the *id-expression* into an access of the corresponding data member. [*Example 2*:

  ```cpp
  auto monad = [](auto v) { return [=] { return v; }; };
  auto bind = [](auto m) {
    return [=](auto fvm) { return fvm(m()); };
  };

  // OK to capture objects with automatic storage duration created during constant expression evaluation.
  static_assert(bind(monad(2))(monad)() == monad(2)());
  ```

  — *end example*] — *end note*]

- a conversion from type *cv* `void*` to a pointer-to-object type;

- a `reinterpret_cast` ([[expr.reinterpret.cast]](expr.reinterpret.cast.md));

- a modification of an object ([[expr.ass]](expr.ass.md), [[expr.post.incr]](expr.post.incr.md), [[expr.pre.incr]](expr.pre.incr.md)) unless it is applied to a non-volatile lvalue of literal type that refers to a non-volatile object whose lifetime began within the evaluation of *E*;

- a *new-expression* ([[expr.new]](expr.new.md)), unless the selected allocation function is a replaceable global allocation function ([[new.delete.single]](../lib/support/new.delete.single.md), [[new.delete.array]](../lib/support/new.delete.array.md)) and the allocated storage is deallocated within the evaluation of *E*;

- a *delete-expression* ([[expr.delete]](expr.delete.md)), unless it deallocates a region of storage allocated within the evaluation of *E*;

- a call to an instance of `std::allocator<T>::allocate` ([[allocator.members]](../lib/utilities/allocator.members.md)), unless the allocated storage is deallocated within the evaluation of *E*;

- a call to an instance of `std::allocator<T>::deallocate` ([[allocator.members]](../lib/utilities/allocator.members.md)), unless it deallocates a region of storage allocated within the evaluation of *E*;

- an *await-expression* ([[expr.await]](expr.await.md));

- a *yield-expression* ([[expr.yield]](expr.yield.md));

- a three-way comparison ([[expr.spaceship]](expr.spaceship.md)), relational ([[expr.rel]](expr.rel.md)), or equality ([[expr.eq]](expr.eq.md)) operator where the result is unspecified;

- a *throw-expression* ([[expr.throw]](expr.throw.md)) or a dynamic cast ([[expr.dynamic.cast]](expr.dynamic.cast.md)) or `typeid` ([[expr.typeid]](expr.typeid.md)) expression that would throw an exception;

- an *asm-declaration* ([[dcl.asm]](../dcl/dcl.asm.md)); or

- an invocation of the `va_arg` macro ([[cstdarg.syn]](../lib/support/cstdarg.syn.md)).

If *E* satisfies the constraints of a core constant expression, but evaluation of *E* would evaluate an operation that has undefined behavior as specified in [[library]](../lib/library/library.md) through [[thread]](../lib/thread/thread.md) of this document, or an invocation of the `va_start` macro ([[cstdarg.syn]](../lib/support/cstdarg.syn.md)), it is unspecified whether *E* is a core constant expression.

[*Example 3*:

```cpp
int x;                              // not constant
struct A {
  constexpr A(bool b) : m(b?42:x) { }
  int m;
};
constexpr int v = A(true).m;        // OK: constructor call initializes m with the value 42

constexpr int w = A(false).m;       // error: initializer for m is x, which is non-constant

constexpr int f1(int k) {
  constexpr int x = k;              // error: x is not initialized by a constant expression
                                    // because lifetime of k began outside the initializer of x
  return x;
}
constexpr int f2(int k) {
  int x = k;                        // OK: not required to be a constant expression
                                    // because x is not constexpr
  return x;
}

constexpr int incr(int &n) {
  return ++n;
}
constexpr int g(int k) {
  constexpr int x = incr(k);        // error: incr(k) is not a core constant expression
                                    // because lifetime of k began outside the expression incr(k)
  return x;
}
constexpr int h(int k) {
  int x = incr(k);                  // OK: incr(k) is not required to be a core constant expression
  return x;
}
constexpr int y = h(1);             // OK: initializes y with the value 2
                                    // h(1) is a core constant expression because
                                    // the lifetime of k begins inside h(1)
```

— *end example*]

**6** For the purposes of determining whether an expression *E* is a core constant expression, the evaluation of a call to a member function of `std::allocator<T>` as defined in [[allocator.members]](../lib/utilities/allocator.members.md), where `T` is a literal type, does not disqualify *E* from being a core constant expression, even if the actual evaluation of such a call would otherwise fail the requirements for a core constant expression. Similarly, the evaluation of a call to `std::destroy_at`, `std::ranges::destroy_at`, `std::construct_at`, or `std::ranges::construct_at` does not disqualify *E* from being a core constant expression unless:

- for a call to `std::construct_at` or `std::ranges::construct_at`, the first argument, of type `T*`, does not point to storage allocated with `std::allocator<T>` or to an object whose lifetime began within the evaluation of *E*, or the evaluation of the underlying constructor call disqualifies *E* from being a core constant expression, or
- for a call to `std::destroy_at` or `std::ranges::destroy_at`, the first argument, of type `T*`, does not point to storage allocated with `std::allocator<T>` or to an object whose lifetime began within the evaluation of *E*, or the evaluation of the underlying destructor call disqualifies *E* from being a core constant expression.

**7** An object `a` is said to have *constant destruction* if:

- it is not of class type nor (possibly multi-dimensional) array thereof, or
- it is of class type or (possibly multi-dimensional) array thereof, that class type has a constexpr destructor, and for a hypothetical expression *E* whose only effect is to destroy `a`, *E* would be a core constant expression if the lifetime of `a` and its non-mutable subobjects (but not its mutable subobjects) were considered to start within *E*.

**8** An *integral constant expression* is an expression of integral or unscoped enumeration type, implicitly converted to a prvalue, where the converted expression is a core constant expression. [*Note 6*: Such expressions may be used as bit-field lengths ([[class.bit]](../class/class.bit.md)), as enumerator initializers if the underlying type is not fixed ([[dcl.enum]](../dcl/dcl.enum.md)), and as alignments ([[dcl.align]](../dcl/dcl.align.md)). — *end note*]

**9** If an expression of literal class type is used in a context where an integral constant expression is required, then that expression is contextually implicitly converted ([[conv]](conv.md)) to an integral or unscoped enumeration type and the selected conversion function shall be `constexpr`. [*Example 4*:

```cpp
struct A {
  constexpr A(int i) : val(i) { }
  constexpr operator int() const { return val; }
  constexpr operator long() const { return 42; }
private:
  int val;
};
constexpr A a = alignof(int);
alignas(a) int n;               // error: ambiguous conversion
struct B { int n : a; };        // error: ambiguous conversion
```

— *end example*]

**10** A *converted constant expression* of type `T` is an expression, implicitly converted to type `T`, where the converted expression is a constant expression and the implicit conversion sequence contains only

- user-defined conversions,
- lvalue-to-rvalue conversions ([[conv.lval]](conv.lval.md)),
- array-to-pointer conversions ([[conv.array]](conv.array.md)),
- function-to-pointer conversions ([[conv.func]](conv.func.md)),
- qualification conversions ([[conv.qual]](conv.qual.md)),
- integral promotions ([[conv.prom]](conv.prom.md)),
- integral conversions ([[conv.integral]](conv.integral.md)) other than narrowing conversions ([[dcl.init.list]](../dcl/dcl.init.list.md)),
- null pointer conversions ([[conv.ptr]](conv.ptr.md)) from `std::nullptr_t`,
- null member pointer conversions ([[conv.mem]](conv.mem.md)) from `std::nullptr_t`, and
- function pointer conversions ([[conv.fctptr]](conv.fctptr.md)),

and where the reference binding (if any) binds directly. [*Note 7*: Such expressions may be used in `new` expressions ([[expr.new]](expr.new.md)), as case expressions ([[stmt.switch]](../stmt/stmt.switch.md)), as enumerator initializers if the underlying type is fixed ([[dcl.enum]](../dcl/dcl.enum.md)), as array bounds ([[dcl.array]](../dcl/dcl.array.md)), and as non-type template arguments ([[temp.arg]](../temp/temp.arg.md)). — *end note*] A *contextually converted constant expression of type `bool`* is an expression, contextually converted to `bool` ([[conv]](conv.md)), where the converted expression is a constant expression and the conversion sequence contains only the conversions above.

**11** A *constant expression* is either a glvalue core constant expression that refers to an entity that is a permitted result of a constant expression (as defined below), or a prvalue core constant expression whose value satisfies the following constraints:

- if the value is an object of class type, each non-static data member of reference type refers to an entity that is a permitted result of a constant expression,
- if the value is of pointer type, it contains the address of an object with static storage duration, the address past the end of such an object ([[expr.add]](expr.add.md)), the address of a non-immediate function, or a null pointer value,
- if the value is of pointer-to-member-function type, it does not designate an immediate function, and
- if the value is an object of class or array type, each subobject satisfies these constraints for the value.

An entity is a *permitted result of a constant expression* if it is an object with static storage duration that either is not a temporary object or is a temporary object whose value satisfies the above constraints, or if it is a non-immediate function. [*Example 5*:

```cpp
consteval int f() { return 42; }
consteval auto g() { return f; }
consteval int h(int (*p)() = g()) { return p(); }
constexpr int r = h();                          // OK
constexpr auto e = g();                         // error: a pointer to an immediate function is
                                                // not a permitted result of a constant expression
```

— *end example*]

**12** [*Note 8*: Since this document imposes no restrictions on the accuracy of floating-point operations, it is unspecified whether the evaluation of a floating-point expression during translation yields the same result as the evaluation of the same expression (or the same operations on the same values) during program execution.[^1] [*Example 6*:

```cpp
bool f() {
    char array[1 + int(1 + 0.2 - 0.1 - 0.1)];   // Must be evaluated during translation
    int size = 1 + int(1 + 0.2 - 0.1 - 0.1);    // May be evaluated at runtime
    return sizeof(array) == size;
}
```

It is unspecified whether the value of `f()` will be `true` or `false`. — *end example*] — *end note*]

**13** An expression or conversion is in an *immediate function context* if it is potentially evaluated and its innermost non-block scope is a function parameter scope of an immediate function. An expression or conversion is an *immediate invocation* if it is a potentially-evaluated explicit or implicit invocation of an immediate function and is not in an immediate function context. An immediate invocation shall be a constant expression.

**14** An expression or conversion is *manifestly constant-evaluated* if it is:

- a *constant-expression*, or

- the condition of a constexpr if statement ([[stmt.if]](../stmt/stmt.if.md)), or

- an immediate invocation, or

- the result of substitution into an atomic constraint expression to determine whether it is satisfied ([[temp.constr.atomic]](../temp/temp.constr.atomic.md)), or

- the initializer of a variable that is usable in constant expressions or has constant initialization.[^2] [*Example 7*:

  ```cpp
  template<bool> struct X {};
  X<std::is_constant_evaluated()> x;                      // type X<true>
  int y;
  const int a = std::is_constant_evaluated() ? y : 1;     // dynamic initialization to 1
  double z[a];                                            // error: a is not usable
                                                          // in constant expressions
  const int b = std::is_constant_evaluated() ? 2 : y;     // static initialization to 2
  int c = y + (std::is_constant_evaluated() ? 2 : y);     // dynamic initialization to y+y

  constexpr int f() {
    const int n = std::is_constant_evaluated() ? 13 : 17; // n is 13
    int m = std::is_constant_evaluated() ? 13 : 17;       // m might be 13 or 17 (see below)
    char arr[n] = {}; // char[13]
    return m + sizeof(arr);
  }
  int p = f();                                            // m is 13; initialized to 26
  int q = p + f();                                        // m is 17 for this call; initialized to 56
  ```

  — *end example*]

[*Note 9*: A manifestly constant-evaluated expression is evaluated even in an unevaluated operand. — *end note*]

**15** An expression or conversion is *potentially constant evaluated* if it is:

- a manifestly constant-evaluated expression,
- a potentially-evaluated expression ([[basic.def.odr]](../basic/basic.def.odr.md)),
- an immediate subexpression of a *braced-init-list*,[^3]
- an expression of the form `&` *cast-expression* that occurs within a templated entity,[^4] or
- a subexpression of one of the above that is not a subexpression of a nested unevaluated operand.

A function or variable is *needed for constant evaluation* if it is:

- a constexpr function that is named by an expression ([[basic.def.odr]](../basic/basic.def.odr.md)) that is potentially constant evaluated, or
- a variable whose name appears as a potentially constant evaluated expression that is either a constexpr variable or is of non-volatile const-qualified integral type or of reference type.

[^1]: Nonetheless, implementations should provide consistent results, irrespective of whether the evaluation was performed during translation and/or during program execution.
[^2]: Testing this condition may involve a trial evaluation of its initializer as described above.
[^3]: Constant evaluation may be necessary to determine whether a narrowing conversion is performed ([[dcl.init.list]](../dcl/dcl.init.list.md)).
[^4]: Constant evaluation may be necessary to determine whether such an expression is value-dependent ([[temp.dep.constexpr]](../temp/temp.dep.constexpr.md)).
