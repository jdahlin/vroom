# 8.8 Declaration statement [stmt.dcl]

[Contents](../README.md) · up: [8 Statements \[stmt.stmt\]](stmt.stmt.md) · prev: [8.7.5 The `goto` statement \[stmt.goto\]](stmt.goto.md) · next: [8.9 Ambiguity resolution \[stmt.ambig\]](stmt.ambig.md)

**1** A declaration statement introduces one or more new identifiers into a block; it has the form

```bnf
declaration-statement:
    block-declaration
```

If an identifier introduced by a declaration was previously declared in an outer block, the outer declaration is hidden for the remainder of the block, after which it resumes its force.

**2** Variables with automatic storage duration ([[basic.stc.auto]](../basic/basic.stc.auto.md)) are initialized each time their *declaration-statement* is executed. Variables with automatic storage duration declared in the block are destroyed on exit from the block ([[stmt.jump]](stmt.jump.md)).

**3** It is possible to transfer into a block, but not in a way that bypasses declarations with initialization (including ones in *condition*s and *init-statement*s). A program that jumps[^1] from a point where a variable with automatic storage duration is not in scope to a point where it is in scope is ill-formed unless the variable has vacuous initialization ([[basic.life]](../basic/basic.life.md)). In such a case, the variables with vacuous initialization are constructed in the order of their declaration. [*Example 1*:

```cpp
void f() {
  // ...
  goto lx;          // error: jump into scope of a
  // ...
ly:
  X a = 1;
  // ...
lx:
  goto ly;          // OK, jump implies destructor call for a followed by
                    // construction again immediately following label ly
}
```

— *end example*]

**4** Dynamic initialization of a block-scope variable with static storage duration ([[basic.stc.static]](../basic/basic.stc.static.md)) or thread storage duration ([[basic.stc.thread]](../basic/basic.stc.thread.md)) is performed the first time control passes through its declaration; such a variable is considered initialized upon the completion of its initialization. If the initialization exits by throwing an exception, the initialization is not complete, so it will be tried again the next time control enters the declaration. If control enters the declaration concurrently while the variable is being initialized, the concurrent execution shall wait for completion of the initialization.[^2] If control re-enters the declaration recursively while the variable is being initialized, the behavior is undefined. [*Example 2*:

```cpp
int foo(int i) {
  static int s = foo(2*i);      // undefined behavior: recursive call
  return i+1;
}
```

— *end example*]

**5** A block-scope object with static or thread storage duration will be destroyed if and only if it was constructed. [*Note 1*: [[basic.start.term]](../basic/basic.start.term.md) describes the order in which block-scope objects with static and thread storage duration are destroyed. — *end note*]

[^1]: The transfer from the condition of a `switch` statement to a `case` label is considered a jump in this respect.
[^2]: The implementation must not introduce any deadlock around execution of the initializer. Deadlocks might still be caused by the program logic; the implementation need only avoid deadlocks due to its own synchronization operations.
