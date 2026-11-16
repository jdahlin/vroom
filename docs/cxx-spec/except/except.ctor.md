# 14.3 Constructors and destructors [except.ctor]

[Contents](../README.md) · up: [14 Exception handling \[except\]](except.md) · prev: [14.2 Throwing an exception \[except.throw\]](except.throw.md) · next: [14.4 Handling an exception \[except.handle\]](except.handle.md)

**1** As control passes from the point where an exception is thrown to a handler, objects with automatic storage duration are destroyed by a process, specified in this subclause, called *stack unwinding*.

**2** Each object with automatic storage duration is destroyed if it has been constructed, but not yet destroyed, since the try block was entered. If an exception is thrown during the destruction of temporaries or local variables for a `return` statement ([[stmt.return]](../stmt/stmt.return.md)), the destructor for the returned object (if any) is also invoked. The objects are destroyed in the reverse order of the completion of their construction. [*Example 1*:

```cpp
struct A { };

struct Y { ~Y() noexcept(false) { throw 0; } };

A f() {
  try {
    A a;
    Y y;
    A b;
    return {};      // #1
  } catch (...) {
  }
  return {};        // #2
}
```

At #1, the returned object of type `A` is constructed. Then, the local variable `b` is destroyed ([[stmt.jump]](../stmt/stmt.jump.md)). Next, the local variable `y` is destroyed, causing stack unwinding, resulting in the destruction of the returned object, followed by the destruction of the local variable `a`. Finally, the returned object is constructed again at #2. — *end example*]

**3** If the initialization or destruction of an object other than by delegating constructor is terminated by an exception, the destructor is invoked for each of the object’s direct subobjects and, for a complete object, virtual base class subobjects, whose initialization has completed ([[dcl.init]](../dcl/dcl.init.md)) and whose destructor has not yet begun execution, except that in the case of destruction, the variant members of a union-like class are not destroyed. [*Note 1*: If such an object has a reference member that extends the lifetime of a temporary object, this ends the lifetime of the reference member, so the lifetime of the temporary object is effectively not extended. — *end note*] The subobjects are destroyed in the reverse order of the completion of their construction. Such destruction is sequenced before entering a handler of the *function-try-block* of the constructor or destructor, if any.

**4** If the *compound-statement* of the *function-body* of a delegating constructor for an object exits via an exception, the object’s destructor is invoked. Such destruction is sequenced before entering a handler of the *function-try-block* of a delegating constructor for that object, if any.

**5** [*Note 2*: If the object was allocated by a *new-expression* ([[expr.new]](../expr/expr.new.md)), the matching deallocation function ([[basic.stc.dynamic.deallocation]](../basic/basic.stc.dynamic.deallocation.md)), if any, is called to free the storage occupied by the object. — *end note*]
