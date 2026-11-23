# 8.7.3 The `return` statement [stmt.return]

[Contents](../README.md) · up: [8.7 Jump statements \[stmt.jump\]](stmt.jump.md) · prev: [8.7.2 The `continue` statement \[stmt.cont\]](stmt.cont.md) · next: [8.7.4 The `co_return` statement \[stmt.return.coroutine\]](stmt.return.coroutine.md)

**1** A function returns to its caller by the `return` statement.

**2** The *expr-or-braced-init-list* of a `return` statement is called its operand. A `return` statement with no operand shall be used only in a function whose return type is *cv* `void`, a constructor ([[class.ctor]](../class/class.ctor.md)), or a destructor ([[class.dtor]](../class/class.dtor.md)). A `return` statement with an operand of type `void` shall be used only in a function whose return type is *cv* `void`. A `return` statement with any other operand shall be used only in a function whose return type is not *cv* `void`; the `return` statement initializes the glvalue result or prvalue result object of the (explicit or implicit) function call by copy-initialization ([[dcl.init]](../dcl/dcl.init.md)) from the operand. [*Note 1*: A `return` statement can involve an invocation of a constructor to perform a copy or move of the operand if it is not a prvalue or if its type differs from the return type of the function. A copy operation associated with a `return` statement may be elided or converted to a move operation if an automatic storage duration variable is returned ([[class.copy.elision]](../class/class.copy.elision.md)). — *end note*] [*Example 1*:

```cpp
std::pair<std::string,int> f(const char* p, int x) {
  return {p,x};
}
```

— *end example*] The destructor for the returned object is potentially invoked ([[class.dtor]](../class/class.dtor.md), [[except.ctor]](../except/except.ctor.md)). [*Example 2*:

```cpp
class A {
  ~A() {}
};
A f() { return A(); }   // error: destructor of A is private (even though it is never invoked)
```

— *end example*] Flowing off the end of a constructor, a destructor, or a non-coroutine function with a *cv* `void` return type is equivalent to a `return` with no operand. Otherwise, flowing off the end of a function other than `main` ([[basic.start.main]](../basic/basic.start.main.md)) or a coroutine ([[dcl.fct.def.coroutine]](../dcl/dcl.fct.def.coroutine.md)) results in undefined behavior.

**3** The copy-initialization of the result of the call is sequenced before the destruction of temporaries at the end of the full-expression established by the operand of the `return` statement, which, in turn, is sequenced before the destruction of local variables ([[stmt.jump]](stmt.jump.md)) of the block enclosing the `return` statement.
