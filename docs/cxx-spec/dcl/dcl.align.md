# 9.12.2 Alignment specifier [dcl.align]

[Contents](../README.md) · up: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · prev: [9.12.1 Attribute syntax and semantics \[dcl.attr.grammar\]](dcl.attr.grammar.md) · next: [9.12.3 Carries dependency attribute \[dcl.attr.depend\]](dcl.attr.depend.md)

**1** An *alignment-specifier* may be applied to a variable or to a class data member, but it shall not be applied to a bit-field, a function parameter, or an *exception-declaration* ([[except.handle]](../except/except.handle.md)). An *alignment-specifier* may also be applied to the declaration of a class (in an *elaborated-type-specifier* ([[dcl.type.elab]](dcl.type.elab.md)) or *class-head* ([[class]](../class/class.md)), respectively). An *alignment-specifier* with an ellipsis is a pack expansion ([[temp.variadic]](../temp/temp.variadic.md)).

**2** When the *alignment-specifier* is of the form `alignas(` *constant-expression* `)`:

- the *constant-expression* shall be an integral constant expression
- if the constant expression does not evaluate to an alignment value ([[basic.align]](../basic/basic.align.md)), or evaluates to an extended alignment and the implementation does not support that alignment in the context of the declaration, the program is ill-formed.

**3** An *alignment-specifier* of the form `alignas(` *type-id* `)` has the same effect as `alignas(alignof(` *type-id* `))` ([[expr.alignof]](../expr/expr.alignof.md)).

**4** The alignment requirement of an entity is the strictest nonzero alignment specified by its *alignment-specifier*s, if any; otherwise, the *alignment-specifier*s have no effect.

**5** The combined effect of all *alignment-specifier*s in a declaration shall not specify an alignment that is less strict than the alignment that would be required for the entity being declared if all *alignment-specifier*s appertaining to that entity were omitted. [*Example 1*:

```cpp
struct alignas(8) S {};
struct alignas(1) U {
  S s;
};  // error: U specifies an alignment that is less strict than if the alignas(1) were omitted.
```

— *end example*]

**6** If the defining declaration of an entity has an *alignment-specifier*, any non-defining declaration of that entity shall either specify equivalent alignment or have no *alignment-specifier*. Conversely, if any declaration of an entity has an *alignment-specifier*, every defining declaration of that entity shall specify an equivalent alignment. No diagnostic is required if declarations of an entity have different *alignment-specifier*s in different translation units. [*Example 2*:

```cpp
// Translation unit #1:
struct S { int x; } s, *p = &s;

// Translation unit #2:
struct alignas(16) S;           // ill-formed, no diagnostic required: definition of S lacks alignment
extern S* p;
```

— *end example*]

**7** [*Example 3*: An aligned buffer with an alignment requirement of `A` and holding `N` elements of type `T` can be declared as:

```cpp
alignas(T) alignas(A) T buffer[N];
```

Specifying `alignas(T)` ensures that the final requested alignment will not be weaker than `alignof(T)`, and therefore the program will not be ill-formed. — *end example*]

**8** [*Example 4*:

```cpp
alignas(double) void f();                           // error: alignment applied to function
alignas(double) unsigned char c[sizeof(double)];    // array of characters, suitably aligned for a double
extern unsigned char c[sizeof(double)];             // no alignas necessary
alignas(float)
  extern unsigned char c[sizeof(double)];           // error: different alignment in declaration
```

— *end example*]
