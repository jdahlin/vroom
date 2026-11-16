# 9.3.3.1 Pointers [dcl.ptr]

[Contents](README.md) · up: [9.3.3 Meaning of declarators \[dcl.meaning\]](dcl.meaning.md) · prev: [9.3.3 Meaning of declarators \[dcl.meaning\]](dcl.meaning.md) · next: [9.3.3.2 References \[dcl.ref\]](dcl.ref.md)

**1** In a declaration `T` `D` where `D` has the form

```bnf
'*' attribute-specifier-seqₒₚₜ cv-qualifier-seqₒₚₜ 'D1'
```

and the type of the identifier in the declaration `T` `D1` is “*derived-declarator-type-list* `T`”, then the type of the identifier of `D` is “*derived-declarator-type-list* *cv-qualifier-seq* pointer to `T`”. The *cv-qualifier*s apply to the pointer and not to the object pointed to. Similarly, the optional *attribute-specifier-seq* ([[dcl.attr.grammar]](dcl.attr.grammar.md)) appertains to the pointer and not to the object pointed to.

**2** [*Example 1*: The declarations

```cpp
const int ci = 10, *pc = &ci, *const cpc = pc, **ppc;
int i, *p, *const cp = &i;
```

declare `ci`, a constant integer; `pc`, a pointer to a constant integer; `cpc`, a constant pointer to a constant integer; `ppc`, a pointer to a pointer to a constant integer; `i`, an integer; `p`, a pointer to integer; and `cp`, a constant pointer to integer. The value of `ci`, `cpc`, and `cp` cannot be changed after initialization. The value of `pc` can be changed, and so can the object pointed to by `cp`. Examples of some correct operations are

```cpp
i = ci;
*cp = ci;
pc++;
pc = cpc;
pc = p;
ppc = &pc;
```

Examples of ill-formed operations are

```cpp
ci = 1;             // error
ci++;               // error
*pc = 2;            // error
cp = &ci;           // error
cpc++;              // error
p = pc;             // error
ppc = &p;           // error
```

Each is unacceptable because it would either change the value of an object declared `const` or allow it to be changed through a cv-unqualified pointer later, for example:

```cpp
*ppc = &ci;         // OK, but would make p point to ci because of previous error
*p = 5;             // clobber ci
```

— *end example*]

**3** See also [[expr.ass]](expr.ass.md) and [[dcl.init]](dcl.init.md).

**4** [*Note 1*: Forming a pointer to reference type is ill-formed; see [[dcl.ref]](dcl.ref.md). Forming a function pointer type is ill-formed if the function type has *cv-qualifier*s or a *ref-qualifier*; see [[dcl.fct]](dcl.fct.md). Since the address of a bit-field ([[class.bit]](class.bit.md)) cannot be taken, a pointer can never point to a bit-field. — *end note*]
