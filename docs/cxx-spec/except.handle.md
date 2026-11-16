# 14.4 Handling an exception [except.handle]

[Contents](README.md) · up: [14 Exception handling \[except\]](except.md) · prev: [14.3 Constructors and destructors \[except.ctor\]](except.ctor.md) · next: [14.5 Exception specifications \[except.spec\]](except.spec.md)

**1** The *exception-declaration* in a *handler* describes the type(s) of exceptions that can cause that *handler* to be entered. The *exception-declaration* shall not denote an incomplete type, an abstract class type, or an rvalue reference type. The *exception-declaration* shall not denote a pointer or reference to an incomplete type, other than `void*`, `const` `void*`, `volatile` `void*`, or `const` `volatile` `void*`.

**2** A handler of type “array of `T`” or function type `T` is adjusted to be of type “pointer to `T`”.

**3** A *handler* is a match for an exception object of type `E` if

- The *handler* is of type *cv* `T` or *cv* `T&` and `E` and `T` are the same type (ignoring the top-level *cv-qualifier*s), or

- the *handler* is of type *cv* `T` or *cv* `T&` and `T` is an unambiguous public base class of `E`, or

- the *handler* is of type *cv* `T` or `const T&` where `T` is a pointer or pointer-to-member type and `E` is a pointer or pointer-to-member type that can be converted to `T` by one or more of

  - a standard pointer conversion ([[conv.ptr]](conv.ptr.md)) not involving conversions to pointers to private or protected or ambiguous classes
  - a function pointer conversion ([[conv.fctptr]](conv.fctptr.md))
  - a qualification conversion ([[conv.qual]](conv.qual.md)), or

- the *handler* is of type *cv* `T` or `const T&` where `T` is a pointer or pointer-to-member type and `E` is `std::nullptr_t`.

[*Note 1*: A *throw-expression* whose operand is an integer literal with value zero does not match a handler of pointer or pointer-to-member type. A handler of reference to array or function type is never a match for any exception object ([[expr.throw]](expr.throw.md)). — *end note*]

[*Example 1*:

```cpp
class Matherr { /* ... */ virtual void vf(); };
class Overflow: public Matherr { /* ... */ };
class Underflow: public Matherr { /* ... */ };
class Zerodivide: public Matherr { /* ... */ };

void f() {
  try {
    g();
  } catch (Overflow oo) {
    // ...
  } catch (Matherr mm) {
    // ...
  }
}
```

Here, the `Overflow` handler will catch exceptions of type `Overflow` and the `Matherr` handler will catch exceptions of type `Matherr` and of all types publicly derived from `Matherr` including exceptions of type `Underflow` and `Zerodivide`. — *end example*]

**4** The handlers for a try block are tried in order of appearance. [*Note 2*: This makes it possible to write handlers that can never be executed, for example by placing a handler for a final derived class after a handler for a corresponding unambiguous public base class. — *end note*]

**5** A `...` in a handler’s *exception-declaration* functions similarly to `...` in a function parameter declaration; it specifies a match for any exception. If present, a `...` handler shall be the last handler for its try block.

**6** If no match is found among the handlers for a try block, the search for a matching handler continues in a dynamically surrounding try block of the same thread.

**7** A handler is considered *active* when initialization is complete for the parameter (if any) of the catch clause. [*Note 3*: The stack will have been unwound at that point. — *end note*] Also, an implicit handler is considered active when the function `std::terminate` is entered due to a throw. A handler is no longer considered active when the catch clause exits.

**8** The exception with the most recently activated handler that is still active is called the *currently handled exception*.

**9** If no matching handler is found, the function `std::terminate` is called; whether or not the stack is unwound before this call to `std::terminate` is implementation-defined ([[except.terminate]](except.terminate.md)).

**10** Referring to any non-static member or base class of an object in the handler for a *function-try-block* of a constructor or destructor for that object results in undefined behavior.

**11** The scope and lifetime of the parameters of a function or constructor extend into the handlers of a *function-try-block*.

**12** Exceptions thrown in destructors of objects with static storage duration or in constructors of namespace-scope objects with static storage duration are not caught by a *function-try-block* on the `main` function ([[basic.start.main]](basic.start.main.md)). Exceptions thrown in destructors of objects with thread storage duration or in constructors of namespace-scope objects with thread storage duration are not caught by a *function-try-block* on the initial function of the thread.

**13** If a `return` statement ([[stmt.return]](stmt.return.md)) appears in a handler of the *function-try-block* of a constructor, the program is ill-formed.

**14** The currently handled exception is rethrown if control reaches the end of a handler of the *function-try-block* of a constructor or destructor. Otherwise, flowing off the end of the *compound-statement* of a *handler* of a *function-try-block* is equivalent to flowing off the end of the *compound-statement* of that function (see [[stmt.return]](stmt.return.md)).

**15** The variable declared by the *exception-declaration*, of type *cv* `T` or *cv* `T&`, is initialized from the exception object, of type `E`, as follows:

- if `T` is a base class of `E`, the variable is copy-initialized ([[dcl.init]](dcl.init.md)) from the corresponding base class subobject of the exception object;
- otherwise, the variable is copy-initialized ([[dcl.init]](dcl.init.md)) from the exception object.

The lifetime of the variable ends when the handler exits, after the destruction of any objects with automatic storage duration initialized within the handler.

**16** When the handler declares an object, any changes to that object will not affect the exception object. When the handler declares a reference to an object, any changes to the referenced object are changes to the exception object and will have effect should that object be rethrown.
