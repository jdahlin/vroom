# 14.2 Throwing an exception [except.throw]

[Contents](../README.md) · up: [14 Exception handling \[except\]](except.md) · prev: [14.1 Preamble \[except.pre\]](except.pre.md) · next: [14.3 Constructors and destructors \[except.ctor\]](except.ctor.md)

**1** Throwing an exception transfers control to a handler. [*Note 1*: An exception can be thrown from one of the following contexts: *throw-expression*s ([[expr.throw]](../expr/expr.throw.md)), allocation functions ([[basic.stc.dynamic.allocation]](../basic/basic.stc.dynamic.allocation.md)), `dynamic_cast` ([[expr.dynamic.cast]](../expr/expr.dynamic.cast.md)), `typeid` ([[expr.typeid]](../expr/expr.typeid.md)), *new-expression*s ([[expr.new]](../expr/expr.new.md)), and standard library functions ([[structure.specifications]](../lib/library/structure.specifications.md)). — *end note*] An object is passed and the type of that object determines which handlers can catch it. [*Example 1*:

```cpp
throw "Help!";
```

can be caught by a *handler* of `const` `char*` type:

```cpp
try {
    // ...
} catch(const char* p) {
    // handle character string exceptions here
}
```

and

```cpp
class Overflow {
public:
    Overflow(char,double,double);
};

void f(double x) {
    throw Overflow('+',x,3.45e107);
}
```

can be caught by a handler for exceptions of type `Overflow`:

```cpp
try {
    f(1.2);
} catch(Overflow& oo) {
    // handle exceptions of type Overflow here
}
```

— *end example*]

**2** When an exception is thrown, control is transferred to the nearest handler with a matching type ([[except.handle]](except.handle.md)); “nearest” means the handler for which the *compound-statement* or *ctor-initializer* following the `try` keyword was most recently entered by the thread of control and not yet exited.

**3** Throwing an exception copy-initializes ([[dcl.init]](../dcl/dcl.init.md), [[class.copy.ctor]](../class/class.copy.ctor.md)) a temporary object, called the *exception object*. An lvalue denoting the temporary is used to initialize the variable declared in the matching *handler* ([[except.handle]](except.handle.md)). If the type of the exception object would be an incomplete type, an abstract class type ([[class.abstract]](../class/class.abstract.md)), or a pointer to an incomplete type other than *cv* `void` the program is ill-formed.

**4** The memory for the exception object is allocated in an unspecified way, except as noted in [[basic.stc.dynamic.allocation]](../basic/basic.stc.dynamic.allocation.md). If a handler exits by rethrowing, control is passed to another handler for the same exception object. The points of potential destruction for the exception object are:

- when an active handler for the exception exits by any means other than rethrowing, immediately after the destruction of the object (if any) declared in the *exception-declaration* in the handler;
- when an object of type `std::exception_ptr` ([[propagation]](../lib/support/propagation.md)) that refers to the exception object is destroyed, before the destructor of `std::exception_ptr` returns.

Among all points of potential destruction for the exception object, there is an unspecified last one where the exception object is destroyed. All other points happen before that last one ([[intro.races]](../basic/intro.races.md)). [*Note 2*: No other thread synchronization is implied in exception handling. — *end note*] The implementation may then deallocate the memory for the exception object; any such deallocation is done in an unspecified way. [*Note 3*: A thrown exception does not propagate to other threads unless caught, stored, and rethrown using appropriate library functions; see [[propagation]](../lib/support/propagation.md) and [[futures]](../lib/thread/futures.md). — *end note*]

**5** When the thrown object is a class object, the constructor selected for the copy-initialization as well as the constructor selected for a copy-initialization considering the thrown object as an lvalue shall be non-deleted and accessible, even if the copy/move operation is elided ([[class.copy.elision]](../class/class.copy.elision.md)). The destructor is potentially invoked ([[class.dtor]](../class/class.dtor.md)).

**6** An exception is considered caught when a handler for that exception becomes active ([[except.handle]](except.handle.md)). [*Note 4*: An exception can have active handlers and still be considered uncaught if it is rethrown. — *end note*]

**7** If the exception handling mechanism handling an uncaught exception ([[except.uncaught]](except.uncaught.md)) directly invokes a function that exits via an exception, the function `std::terminate` is called ([[except.terminate]](except.terminate.md)). [*Example 2*:

```cpp
struct C {
  C() { }
  C(const C&) {
    if (std::uncaught_exceptions()) {
      throw 0;      // throw during copy to handler's exception-declaration object ([except.handle])
    }
  }
};

int main() {
  try {
    throw C();      // calls std::terminate if construction of the handler's
                    // exception-declaration object is not elided ([class.copy.elision])
  } catch(C) { }
}
```

— *end example*] [*Note 5*: Consequently, destructors should generally catch exceptions and not let them propagate. — *end note*]
