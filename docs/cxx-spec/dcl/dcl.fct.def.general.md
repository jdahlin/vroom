# 9.5.1 In general [dcl.fct.def.general]

[Contents](../README.md) · up: [9.5 Function definitions \[dcl.fct.def\]](dcl.fct.def.md) · prev: [9.5 Function definitions \[dcl.fct.def\]](dcl.fct.def.md) · next: [9.5.2 Explicitly-defaulted functions \[dcl.fct.def.default\]](dcl.fct.def.default.md)

**1** Function definitions have the form

```bnf
function-definition:
    attribute-specifier-seqₒₚₜ decl-specifier-seqₒₚₜ declarator virt-specifier-seqₒₚₜ function-body
    attribute-specifier-seqₒₚₜ decl-specifier-seqₒₚₜ declarator requires-clause function-body
```

```bnf
function-body:
    ctor-initializerₒₚₜ compound-statement
    function-try-block
    '=' 'default' ';'
    '=' 'delete' ';'
```

Any informal reference to the body of a function should be interpreted as a reference to the non-terminal *function-body*. The optional *attribute-specifier-seq* in a *function-definition* appertains to the function. A *virt-specifier-seq* can be part of a *function-definition* only if it is a *member-declaration* ([[class.mem]](../class/class.mem.md)).

**2** In a *function-definition*, either `void` *declarator* `;` or *declarator* `;` shall be a well-formed function declaration as described in [[dcl.fct]](dcl.fct.md). A function shall be defined only in namespace or class scope. The type of a parameter or the return type for a function definition shall not be a (possibly cv-qualified) class type that is incomplete or abstract within the function body unless the function is deleted ([[dcl.fct.def.delete]](dcl.fct.def.delete.md)).

**3** [*Example 1*: A simple example of a complete function definition is

```cpp
int max(int a, int b, int c) {
  int m = (a > b) ? a : b;
  return (m > c) ? m : c;
}
```

Here `int` is the *decl-specifier-seq*; `max(int` `a,` `int` `b,` `int` `c)` is the *declarator*; `{ /* ... */ }` is the *function-body*. — *end example*]

**4** A *ctor-initializer* is used only in a constructor; see [[class.ctor]](../class/class.ctor.md) and [[class.init]](../class/class.init.md).

**5** [*Note 1*: A *cv-qualifier-seq* affects the type of `this` in the body of a member function; see [[dcl.ref]](dcl.ref.md). — *end note*]

**6** [*Note 2*: Unused parameters need not be named. For example,

```cpp
void print(int a, int) {
  std::printf("a = %d\n",a);
}
```

— *end note*]

**7** In the *function-body*, a *function-local predefined variable* denotes a block-scope object of static storage duration that is implicitly defined (see [[basic.scope.block]](../basic/basic.scope.block.md)).

**8** The function-local predefined variable `__func__` is defined as if a definition of the form

```cpp
static const char __func__[] = "function-name";
```

had been provided, where `function-name` is an implementation-defined string. It is unspecified whether such a variable has an address distinct from that of any other object in the program.[^1] [*Example 2*:

```cpp
struct S {
  S() : s(__func__) { }             // OK
  const char* s;
};
void f(const char* s = __func__);   // error: __func__ is undeclared
```

— *end example*]

[^1]: Implementations are permitted to provide additional predefined variables with names that are reserved to the implementation ([[lex.name]](../lex/lex.name.md)). If a predefined variable is not odr-used ([[basic.def.odr]](../basic/basic.def.odr.md)), its string value need not be present in the program image.
