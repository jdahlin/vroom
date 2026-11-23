# 11.10.5 Copy/move elision [class.copy.elision]

[Contents](../README.md) · up: [11.10 Initialization \[class.init\]](class.init.md) · prev: [11.10.4 Construction and destruction \[class.cdtor\]](class.cdtor.md) · next: [11.11 Comparisons \[class.compare\]](class.compare.md)

**1** When certain criteria are met, an implementation is allowed to omit the copy/move construction of a class object, even if the constructor selected for the copy/move operation and/or the destructor for the object have side effects. In such cases, the implementation treats the source and target of the omitted copy/move operation as simply two different ways of referring to the same object. If the first parameter of the selected constructor is an rvalue reference to the object’s type, the destruction of that object occurs when the target would have been destroyed; otherwise, the destruction occurs at the later of the times when the two objects would have been destroyed without the optimization.[^1] This elision of copy/move operations, called *copy elision*, is permitted in the following circumstances (which may be combined to eliminate multiple copies):

- in a `return` statement in a function with a class return type, when the *expression* is the name of a non-volatile object with automatic storage duration (other than a function parameter or a variable introduced by the *exception-declaration* of a *handler* ([[except.handle]](../except/except.handle.md))) with the same type (ignoring cv-qualification) as the function return type, the copy/move operation can be omitted by constructing the object directly into the function call’s return object
- in a *throw-expression* ([[expr.throw]](../expr/expr.throw.md)), when the operand is the name of a non-volatile object with automatic storage duration (other than a function or catch-clause parameter) whose scope does not extend beyond the end of the innermost enclosing *try-block* (if there is one), the copy/move operation can be omitted by constructing the object directly into the exception object
- in a coroutine ([[dcl.fct.def.coroutine]](../dcl/dcl.fct.def.coroutine.md)), a copy of a coroutine parameter can be omitted and references to that copy replaced with references to the corresponding parameter if the meaning of the program will be unchanged except for the execution of a constructor and destructor for the parameter copy object
- when the *exception-declaration* of an exception handler ([[except.pre]](../except/except.pre.md)) declares an object of the same type (except for cv-qualification) as the exception object ([[except.throw]](../except/except.throw.md)), the copy operation can be omitted by treating the *exception-declaration* as an alias for the exception object if the meaning of the program will be unchanged except for the execution of constructors and destructors for the object declared by the *exception-declaration*. [*Note 1*: There cannot be a move from the exception object because it is always an lvalue. — *end note*]

Copy elision is not permitted where an expression is evaluated in a context requiring a constant expression ([[expr.const]](../expr/expr.const.md)) and in constant initialization ([[basic.start.static]](../basic/basic.start.static.md)). [*Note 2*: Copy elision might be performed if the same expression is evaluated in another context. — *end note*]

**2** [*Example 1*:

```cpp
class Thing {
public:
  Thing();
  ~Thing();
  Thing(const Thing&);
};

Thing f() {
  Thing t;
  return t;
}

Thing t2 = f();

struct A {
  void *p;
  constexpr A(): p(this) {}
};

constexpr A g() {
  A loc;
  return loc;
}

constexpr A a;          // well-formed, a.p points to a
constexpr A b = g();    // error: b.p would be dangling ([expr.const])

void h() {
  A c = g();            // well-formed, c.p may point to c or to an ephemeral temporary
}
```

Here the criteria for elision can eliminate the copying of the object `t` with automatic storage duration into the result object for the function call `f()`, which is the global object `t2`. Effectively, the construction of the local object `t` can be viewed as directly initializing the global object `t2`, and that object’s destruction will occur at program exit. Adding a move constructor to `Thing` has the same effect, but it is the move construction from the object with automatic storage duration to `t2` that is elided. — *end example*]

**3** An *implicitly movable entity* is a variable of automatic storage duration that is either a non-volatile object or an rvalue reference to a non-volatile object type. In the following copy-initialization contexts, a move operation might be used instead of a copy operation:

- If the *expression* in a `return` ([[stmt.return]](../stmt/stmt.return.md)) or `co_return` ([[stmt.return.coroutine]](../stmt/stmt.return.coroutine.md)) statement is a (possibly parenthesized) *id-expression* that names an implicitly movable entity declared in the body or *parameter-declaration-clause* of the innermost enclosing function or *lambda-expression*, or
- if the operand of a *throw-expression* ([[expr.throw]](../expr/expr.throw.md)) is a (possibly parenthesized) *id-expression* that names an implicitly movable entity whose scope does not extend beyond the *compound-statement* of the innermost *try-block* or *function-try-block* (if any) whose *compound-statement* or *ctor-initializer* encloses the *throw-expression*,

overload resolution to select the constructor for the copy or the `return_value` overload to call is first performed as if the expression or operand were an rvalue. If the first overload resolution fails or was not performed, overload resolution is performed again, considering the expression or operand as an lvalue. [*Note 3*: This two-stage overload resolution must be performed regardless of whether copy elision will occur. It determines the constructor or the `return_value` overload to be called if elision is not performed, and the selected constructor or `return_value` overload must be accessible even if the call is elided. — *end note*]

**4** [*Example 2*:

```cpp
class Thing {
public:
  Thing();
  ~Thing();
  Thing(Thing&&);
private:
  Thing(const Thing&);
};

Thing f(bool b) {
  Thing t;
  if (b)
    throw t;            // OK: Thing(Thing&&) used (or elided) to throw t
  return t;             // OK: Thing(Thing&&) used (or elided) to return t
}

Thing t2 = f(false);    // OK: no extra copy/move performed, t2 constructed by call to f

struct Weird {
  Weird();
  Weird(Weird&);
};

Weird g() {
  Weird w;
  return w;             // OK: first overload resolution fails, second overload resolution selects Weird(Weird&)
}
```

— *end example*]

**5** [*Example 3*:

```cpp
template<class T> void g(const T&);

template<class T> void f() {
  T x;
  try {
    T y;
    try { g(x); }
    catch (...) {
      if (/*...*/)
        throw x;        // does not move
      throw y;          // moves
    }
    g(y);
  } catch(...) {
    g(x);
    g(y);               // error: y is not in scope
  }
}
```

— *end example*]

[^1]: Because only one object is destroyed instead of two, and one copy/move constructor is not executed, there is still one object destroyed for each one constructed.
