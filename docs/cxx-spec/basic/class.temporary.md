# 6.7.7 Temporary objects [class.temporary]

[Contents](../README.md) · up: [6.7 Memory and objects \[basic.memobj\]](basic.memobj.md) · prev: [6.7.6 Alignment \[basic.align\]](basic.align.md) · next: [6.8 Types \[basic.types\]](basic.types.md)

**1** Temporary objects are created

- when a prvalue is converted to an xvalue ([[conv.rval]](../expr/conv.rval.md)),
- when needed by the implementation to pass or return an object of trivially-copyable type (see below), and
- when throwing an exception ([[except.throw]](../except/except.throw.md)). [*Note 1*: The lifetime of exception objects is described in [[except.throw]](../except/except.throw.md). — *end note*]

Even when the creation of the temporary object is unevaluated ([[expr.prop]](../expr/expr.prop.md)), all the semantic restrictions shall be respected as if the temporary object had been created and later destroyed. [*Note 2*: This includes accessibility ([[class.access]](../class/class.access.md)) and whether it is deleted, for the constructor selected and for the destructor. However, in the special case of the operand of a *decltype-specifier* ([[expr.call]](../expr/expr.call.md)), no temporary is introduced, so the foregoing does not apply to such a prvalue. — *end note*]

**2** The materialization of a temporary object is generally delayed as long as possible in order to avoid creating unnecessary temporary objects. [*Note 3*: Temporary objects are materialized:

- when binding a reference to a prvalue ([[dcl.init.ref]](../dcl/dcl.init.ref.md), [[expr.type.conv]](../expr/expr.type.conv.md), [[expr.dynamic.cast]](../expr/expr.dynamic.cast.md), [[expr.static.cast]](../expr/expr.static.cast.md), [[expr.const.cast]](../expr/expr.const.cast.md), [[expr.cast]](../expr/expr.cast.md)),
- when performing member access on a class prvalue ([[expr.ref]](../expr/expr.ref.md), [[expr.mptr.oper]](../expr/expr.mptr.oper.md)),
- when performing an array-to-pointer conversion or subscripting on an array prvalue ([[conv.array]](../expr/conv.array.md), [[expr.sub]](../expr/expr.sub.md)),
- when initializing an object of type `std::initializer_list<T>` from a *braced-init-list* ([[dcl.init.list]](../dcl/dcl.init.list.md)),
- for certain unevaluated operands ([[expr.typeid]](../expr/expr.typeid.md), [[expr.sizeof]](../expr/expr.sizeof.md)), and
- when a prvalue that has type other than *cv* `void` appears as a discarded-value expression ([[expr.prop]](../expr/expr.prop.md)).

— *end note*] [*Example 1*: Consider the following code:

```cpp
class X {
public:
  X(int);
  X(const X&);
  X& operator=(const X&);
  ~X();
};

class Y {
public:
  Y(int);
  Y(Y&&);
  ~Y();
};

X f(X);
Y g(Y);

void h() {
  X a(1);
  X b = f(X(2));
  Y c = g(Y(3));
  a = f(a);
}
```

`X(2)` is constructed in the space used to hold `f()`’s argument and `Y(3)` is constructed in the space used to hold `g()`’s argument. Likewise, `f()`’s result is constructed directly in `b` and `g()`’s result is constructed directly in `c`. On the other hand, the expression `a = f(a)` requires a temporary for the result of `f(a)`, which is materialized so that the reference parameter of `X::operator=(const X&)` can bind to it. — *end example*]

**3** When an object of class type `X` is passed to or returned from a function, if `X` has at least one eligible copy or move constructor ([[special]](../class/special.md)), each such constructor is trivial, and the destructor of `X` is either trivial or deleted, implementations are permitted to create a temporary object to hold the function parameter or result object. The temporary object is constructed from the function argument or return value, respectively, and the function’s parameter or return object is initialized as if by using the eligible trivial constructor to copy the temporary (even if that constructor is inaccessible or would not be selected by overload resolution to perform a copy or move of the object). [*Note 4*: This latitude is granted to allow objects of class type to be passed to or returned from functions in registers. — *end note*]

**4** When an implementation introduces a temporary object of a class that has a non-trivial constructor ([[class.default.ctor]](../class/class.default.ctor.md), [[class.copy.ctor]](../class/class.copy.ctor.md)), it shall ensure that a constructor is called for the temporary object. Similarly, the destructor shall be called for a temporary with a non-trivial destructor ([[class.dtor]](../class/class.dtor.md)). Temporary objects are destroyed as the last step in evaluating the full-expression ([[intro.execution]](intro.execution.md)) that (lexically) contains the point where they were created. This is true even if that evaluation ends in throwing an exception. The value computations and side effects of destroying a temporary object are associated only with the full-expression, not with any specific subexpression.

**5** There are three contexts in which temporaries are destroyed at a different point than the end of the full-expression. The first context is when a default constructor is called to initialize an element of an array with no corresponding initializer ([[dcl.init]](../dcl/dcl.init.md)). The second context is when a copy constructor is called to copy an element of an array while the entire array is copied ([[expr.prim.lambda.capture]](../expr/expr.prim.lambda.capture.md), [[class.copy.ctor]](../class/class.copy.ctor.md)). In either case, if the constructor has one or more default arguments, the destruction of every temporary created in a default argument is sequenced before the construction of the next array element, if any.

**6** The third context is when a reference is bound to a temporary object.[^1] The temporary object to which the reference is bound or the temporary object that is the complete object of a subobject to which the reference is bound persists for the lifetime of the reference if the glvalue to which the reference is bound was obtained through one of the following:

- a temporary materialization conversion ([[conv.rval]](../expr/conv.rval.md)),

- `(` *expression* `)`, where *expression* is one of these expressions,

- subscripting ([[expr.sub]](../expr/expr.sub.md)) of an array operand, where that operand is one of these expressions,

- a class member access ([[expr.ref]](../expr/expr.ref.md)) using the `.` operator where the left operand is one of these expressions and the right operand designates a non-static data member of non-reference type,

- a pointer-to-member operation ([[expr.mptr.oper]](../expr/expr.mptr.oper.md)) using the `.*` operator where the left operand is one of these expressions and the right operand is a pointer to data member of non-reference type,

- a

  - `const_cast` ([[expr.const.cast]](../expr/expr.const.cast.md)),
  - `static_cast` ([[expr.static.cast]](../expr/expr.static.cast.md)),
  - `dynamic_cast` ([[expr.dynamic.cast]](../expr/expr.dynamic.cast.md)), or
  - `reinterpret_cast` ([[expr.reinterpret.cast]](../expr/expr.reinterpret.cast.md))

  converting, without a user-defined conversion, a glvalue operand that is one of these expressions to a glvalue that refers to the object designated by the operand, or to its complete object or a subobject thereof,

- a conditional expression ([[expr.cond]](../expr/expr.cond.md)) that is a glvalue where the second or third operand is one of these expressions, or

- a comma expression ([[expr.comma]](../expr/expr.comma.md)) that is a glvalue where the right operand is one of these expressions.

[*Example 2*:

```cpp
template<typename T> using id = T;

int i = 1;
int&& a = id<int[3]>{1, 2, 3}[i];           // temporary array has same lifetime as a
const int& b = static_cast<const int&>(0);  // temporary int has same lifetime as b
int&& c = cond ? id<int[3]>{1, 2, 3}[i] : static_cast<int&&>(0);
                                            // exactly one of the two temporaries is lifetime-extended
```

— *end example*] [*Note 5*: An explicit type conversion ([[expr.type.conv]](../expr/expr.type.conv.md), [[expr.cast]](../expr/expr.cast.md)) is interpreted as a sequence of elementary casts, covered above. [*Example 3*:

```cpp
const int& x = (const int&)1;   // temporary for value 1 has same lifetime as x
```

— *end example*] — *end note*] [*Note 6*: If a temporary object has a reference member initialized by another temporary object, lifetime extension applies recursively to such a member’s initializer. [*Example 4*:

```cpp
struct S {
  const int& m;
};
const S& s = S{1};              // both S and int temporaries have lifetime of s
```

— *end example*] — *end note*]

The exceptions to this lifetime rule are:

- A temporary object bound to a reference parameter in a function call ([[expr.call]](../expr/expr.call.md)) persists until the completion of the full-expression containing the call.

- A temporary object bound to a reference element of an aggregate of class type initialized from a parenthesized *expression-list* ([[dcl.init]](../dcl/dcl.init.md)) persists until the completion of the full-expression containing the *expression-list*.

- The lifetime of a temporary bound to the returned value in a function `return` statement ([[stmt.return]](../stmt/stmt.return.md)) is not extended; the temporary is destroyed at the end of the full-expression in the `return` statement.

- A temporary bound to a reference in a *new-initializer* ([[expr.new]](../expr/expr.new.md)) persists until the completion of the full-expression containing the *new-initializer*. [*Note 7*: This may introduce a dangling reference. — *end note*] [*Example 5*:

  ```cpp
  struct S { int mi; const std::pair<int,int>& mp; };
  S a { 1, {2,3} };
  S* p = new S{ 1, {2,3} };       // creates dangling reference
  ```

  — *end example*]

**7** The destruction of a temporary whose lifetime is not extended by being bound to a reference is sequenced before the destruction of every temporary which is constructed earlier in the same full-expression. If the lifetime of two or more temporaries to which references are bound ends at the same point, these temporaries are destroyed at that point in the reverse order of the completion of their construction. In addition, the destruction of temporaries bound to references shall take into account the ordering of destruction of objects with static, thread, or automatic storage duration ([[basic.stc.static]](basic.stc.static.md), [[basic.stc.thread]](basic.stc.thread.md), [[basic.stc.auto]](basic.stc.auto.md)); that is, if `obj1` is an object with the same storage duration as the temporary and created before the temporary is created the temporary shall be destroyed before `obj1` is destroyed; if `obj2` is an object with the same storage duration as the temporary and created after the temporary is created the temporary shall be destroyed after `obj2` is destroyed.

**8** [*Example 6*:

```cpp
struct S {
  S();
  S(int);
  friend S operator+(const S&, const S&);
  ~S();
};
S obj1;
const S& cr = S(16)+S(23);
S obj2;
```

The expression `S(16) + S(23)` creates three temporaries: a first temporary `T1` to hold the result of the expression `S(16)`, a second temporary `T2` to hold the result of the expression `S(23)`, and a third temporary `T3` to hold the result of the addition of these two expressions. The temporary `T3` is then bound to the reference `cr`. It is unspecified whether `T1` or `T2` is created first. On an implementation where `T1` is created before `T2`, `T2` shall be destroyed before `T1`. The temporaries `T1` and `T2` are bound to the reference parameters of `operator+`; these temporaries are destroyed at the end of the full-expression containing the call to `operator+`. The temporary `T3` bound to the reference `cr` is destroyed at the end of `cr`’s lifetime, that is, at the end of the program. In addition, the order in which `T3` is destroyed takes into account the destruction order of other objects with static storage duration. That is, because `obj1` is constructed before `T3`, and `T3` is constructed before `obj2`, `obj2` shall be destroyed before `T3`, and `T3` shall be destroyed before `obj1`. — *end example*]

[^1]: The same rules apply to initialization of an `initializer_list` object ([[dcl.init.list]](../dcl/dcl.init.list.md)) with its underlying temporary array.
