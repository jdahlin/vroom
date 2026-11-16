# 6.4.3 Block scope [basic.scope.block]

[Contents](README.md) · up: [6.4 Scope \[basic.scope\]](basic.scope.md) · prev: [6.4.2 Point of declaration \[basic.scope.pdecl\]](basic.scope.pdecl.md) · next: [6.4.4 Function parameter scope \[basic.scope.param\]](basic.scope.param.md)

**1** A name declared in a block ([[stmt.block]](stmt.block.md)) is local to that block; it has *block scope*. Its potential scope begins at its point of declaration ([[basic.scope.pdecl]](basic.scope.pdecl.md)) and ends at the end of its block. A variable declared at block scope is a *local variable*.

**2** The name declared in an *exception-declaration* is local to the *handler* and shall not be redeclared in the outermost block of the *handler*.

**3** Names declared in the *init-statement*, the *for-range-declaration*, and in the *condition* of `if`, `while`, `for`, and `switch` statements are local to the `if`, `while`, `for`, or `switch` statement (including the controlled statement), and shall not be redeclared in a subsequent condition of that statement nor in the outermost block (or, for the `if` statement, any of the outermost blocks) of the controlled statement. [*Example 1*:

```cpp
if (int x = f()) {
  int x;            // error: redeclaration of x
}
else {
  int x;            // error: redeclaration of x
}
```

— *end example*]
