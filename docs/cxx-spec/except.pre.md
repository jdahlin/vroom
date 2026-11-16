# 14.1 Preamble [except.pre]

[Contents](README.md) · up: [14 Exception handling \[except\]](except.md) · prev: [14 Exception handling \[except\]](except.md) · next: [14.2 Throwing an exception \[except.throw\]](except.throw.md)

**1** Exception handling provides a way of transferring control and information from a point in the execution of a thread to an exception handler associated with a point previously passed by the execution. A handler will be invoked only by throwing an exception in code executed in the handler’s try block or in functions called from the handler’s try block.

```bnf
try-block:
    'try' compound-statement handler-seq
```

```bnf
function-try-block:
    'try' ctor-initializerₒₚₜ compound-statement handler-seq
```

```bnf
handler-seq:
    handler handler-seqₒₚₜ
```

```bnf
handler:
    'catch' '(' exception-declaration ')' compound-statement
```

```bnf
exception-declaration:
    attribute-specifier-seqₒₚₜ type-specifier-seq declarator
    attribute-specifier-seqₒₚₜ type-specifier-seq abstract-declaratorₒₚₜ
    '...'
```

The optional *attribute-specifier-seq* in an *exception-declaration* appertains to the parameter of the catch clause ([[except.handle]](except.handle.md)).

**2** A *try-block* is a *statement* ([[stmt.pre]](stmt.pre.md)). [*Note 1*: Within this Clause “try block” is taken to mean both *try-block* and *function-try-block*. — *end note*]

**3** A `goto` or `switch` statement shall not be used to transfer control into a try block or into a handler. [*Example 1*:

```cpp
void f() {
  goto l1;          // error
  goto l2;          // error
  try {
    goto l1;        // OK
    goto l2;        // error
    l1: ;
  } catch (...) {
    l2: ;
    goto l1;        // error
    goto l2;        // OK
  }
}
```

— *end example*] A `goto`, `break`, `return`, or `continue` statement can be used to transfer control out of a try block or handler. When this happens, each variable declared in the try block will be destroyed in the context that directly contains its declaration. [*Example 2*:

```cpp
lab:  try {
  T1 t1;
  try {
    T2 t2;
    if (condition)
      goto lab;
    } catch(...) { /* handler 2 */ }
  } catch(...) { /* handler 1 */ }
```

Here, executing `goto lab;` will destroy first `t2`, then `t1`, assuming the *condition* does not declare a variable. Any exception thrown while destroying `t2` will result in executing `handler 2`; any exception thrown while destroying `t1` will result in executing `handler 1`. — *end example*]

**4** A *function-try-block* associates a *handler-seq* with the *ctor-initializer*, if present, and the *compound-statement*. An exception thrown during the execution of the *compound-statement* or, for constructors and destructors, during the initialization or destruction, respectively, of the class’s subobjects, transfers control to a handler in a *function-try-block* in the same way as an exception thrown during the execution of a *try-block* transfers control to other handlers. [*Example 3*:

```cpp
int f(int);
class C {
  int i;
  double d;
public:
  C(int, double);
};

C::C(int ii, double id)
try : i(f(ii)), d(id) {
    // constructor statements
} catch (...) {
    // handles exceptions thrown from the ctor-initializer and from the constructor statements
}
```

— *end example*]

**5** In this Clause, “before” and “after” refer to the “sequenced before” relation ([[intro.execution]](intro.execution.md)).
