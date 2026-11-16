# 13.7.3 Variadic templates [temp.variadic]

[Contents](../README.md) · up: [13.7 Template declarations \[temp.decls\]](temp.decls.md) · prev: [13.7.2 Member templates \[temp.mem\]](temp.mem.md) · next: [13.7.4 Friends \[temp.friend\]](temp.friend.md)

**1** A *template parameter pack* is a template parameter that accepts zero or more template arguments. [*Example 1*:

```cpp
template<class ... Types> struct Tuple { };

Tuple<> t0;                     // Types contains no arguments
Tuple<int> t1;                  // Types contains one argument: int
Tuple<int, float> t2;           // Types contains two arguments: int and float
Tuple<0> error;                 // error: 0 is not a type
```

— *end example*]

**2** A *function parameter pack* is a function parameter that accepts zero or more function arguments. [*Example 2*:

```cpp
template<class ... Types> void f(Types ... args);

f();                            // args contains no arguments
f(1);                           // args contains one argument: int
f(2, 1.0);                      // args contains two arguments: int and double
```

— *end example*]

**3** An **init-capture* pack* is a lambda capture that introduces an *init-capture* for each of the elements in the pack expansion of its *initializer*. [*Example 3*:

```cpp
template <typename... Args>
void foo(Args... args) {
    [...xs=args]{
        bar(xs...);             // xs is an init-capture pack
    };
}

foo();                          // xs contains zero init-captures
foo(1);                         // xs contains one init-capture
```

— *end example*]

**4** A *pack* is a template parameter pack, a function parameter pack, or an *init-capture* pack. The number of elements of a template parameter pack or a function parameter pack is the number of arguments provided for the parameter pack. The number of elements of an *init-capture* pack is the number of elements in the pack expansion of its *initializer*.

**5** A *pack expansion* consists of a *pattern* and an ellipsis, the instantiation of which produces zero or more instantiations of the pattern in a list (described below). The form of the pattern depends on the context in which the expansion occurs. Pack expansions can occur in the following contexts:

- In a function parameter pack ([[dcl.fct]](../dcl/dcl.fct.md)); the pattern is the *parameter-declaration* without the ellipsis.

- In a *using-declaration* ([[namespace.udecl]](../dcl/namespace.udecl.md)); the pattern is a *using-declarator*.

- In a template parameter pack that is a pack expansion ([[temp.param]](temp.param.md)):

  - if the template parameter pack is a *parameter-declaration*; the pattern is the *parameter-declaration* without the ellipsis;
  - if the template parameter pack is a *type-parameter*; the pattern is the corresponding *type-parameter* without the ellipsis.

- In an *initializer-list* ([[dcl.init]](../dcl/dcl.init.md)); the pattern is an *initializer-clause*.

- In a *base-specifier-list* ([[class.derived]](../class/class.derived.md)); the pattern is a *base-specifier*.

- In a *mem-initializer-list* ([[class.base.init]](../class/class.base.init.md)) for a *mem-initializer* whose *mem-initializer-id* denotes a base class; the pattern is the *mem-initializer*.

- In a *template-argument-list* ([[temp.arg]](temp.arg.md)); the pattern is a *template-argument*.

- In an *attribute-list* ([[dcl.attr.grammar]](../dcl/dcl.attr.grammar.md)); the pattern is an *attribute*.

- In an *alignment-specifier* ([[dcl.align]](../dcl/dcl.align.md)); the pattern is the *alignment-specifier* without the ellipsis.

- In a *capture-list* ([[expr.prim.lambda.capture]](../expr/expr.prim.lambda.capture.md)); the pattern is the *capture* without the ellipsis.

- In a `sizeof...` expression ([[expr.sizeof]](../expr/expr.sizeof.md)); the pattern is an *identifier*.

- In a *fold-expression* ([[expr.prim.fold]](../expr/expr.prim.fold.md)); the pattern is the *cast-expression* that contains an unexpanded pack.

[*Example 4*:

```cpp
template<class ... Types> void f(Types ... rest);
template<class ... Types> void g(Types ... rest) {
  f(&rest ...);     // ``&rest ...'' is a pack expansion; ``&rest'' is its pattern
}
```

— *end example*]

**6** For the purpose of determining whether a pack satisfies a rule regarding entities other than packs, the pack is considered to be the entity that would result from an instantiation of the pattern in which it appears.

**7** A pack whose name appears within the pattern of a pack expansion is expanded by that pack expansion. An appearance of the name of a pack is only expanded by the innermost enclosing pack expansion. The pattern of a pack expansion shall name one or more packs that are not expanded by a nested pack expansion; such packs are called *unexpanded packs* in the pattern. All of the packs expanded by a pack expansion shall have the same number of arguments specified. An appearance of a name of a pack that is not expanded is ill-formed. [*Example 5*:

```cpp
template<typename...> struct Tuple {};
template<typename T1, typename T2> struct Pair {};

template<class ... Args1> struct zip {
  template<class ... Args2> struct with {
    typedef Tuple<Pair<Args1, Args2> ... > type;
  };
};

typedef zip<short, int>::with<unsigned short, unsigned>::type T1;
    // T1 is Tuple<Pair<short, unsigned short>, Pair<int, unsigned>>
typedef zip<short>::with<unsigned short, unsigned>::type T2;
    // error: different number of arguments specified for Args1 and Args2

template<class ... Args>
  void g(Args ... args) {                   // OK: Args is expanded by the function parameter pack args
    f(const_cast<const Args*>(&args)...);   // OK: ``Args'' and ``args'' are expanded
    f(5 ...);                               // error: pattern does not contain any packs
    f(args);                                // error: pack ``args'' is not expanded
    f(h(args ...) + args ...);              // OK: first ``args'' expanded within h,
                                            // second ``args'' expanded within f
  }
```

— *end example*]

**8** The instantiation of a pack expansion that is neither a `sizeof...` expression nor a *fold-expression* produces a list of elements `E`<sub>1</sub>, `E`<sub>2</sub>, …, `E`<sub>*N*</sub>, where *N* is the number of elements in the pack expansion parameters. Each `E`<sub>*i*</sub> is generated by instantiating the pattern and replacing each pack expansion parameter with its *i*<sup></sup>*th* element. Such an element, in the context of the instantiation, is interpreted as follows:

- if the pack is a template parameter pack, the element is a template parameter ([[temp.param]](temp.param.md)) of the corresponding kind (type or non-type) designating the *i*<sup></sup>*th* corresponding type or value template argument;
- if the pack is a function parameter pack, the element is an *id-expression* designating the *i*<sup></sup>*th* function parameter that resulted from instantiation of the function parameter pack declaration; otherwise
- if the pack is an *init-capture* pack, the element is an *id-expression* designating the variable introduced by the *i*<sup></sup>*th* *init-capture* that resulted from instantiation of the *init-capture* pack.

All of the `E`<sub>*i*</sub> become items in the enclosing list. [*Note 1*: The variety of list varies with the context: *expression-list*, *base-specifier-list*, *template-argument-list*, etc. — *end note*] When *N* is zero, the instantiation of the expansion produces an empty list. Such an instantiation does not alter the syntactic interpretation of the enclosing construct, even in cases where omitting the list entirely would otherwise be ill-formed or would result in an ambiguity in the grammar. [*Example 6*:

```cpp
template<class... T> struct X : T... { };
template<class... T> void f(T... values) {
  X<T...> x(values...);
}

template void f<>();    // OK: X<> has no base classes
                        // x is a variable of type X<> that is value-initialized
```

— *end example*]

**9** The instantiation of a `sizeof...` expression ([[expr.sizeof]](../expr/expr.sizeof.md)) produces an integral constant containing the number of elements in the pack it expands.

**10** The instantiation of a *fold-expression* produces:

- `((``E`<sub>1</sub> *op* `E`<sub>2</sub>`)` *op* …`)` *op* `E`<sub>*N*</sub> for a unary left fold,
- `E`<sub>1</sub> *op* `(`… *op* `(``E`<sub>*N*-1</sub> *op* `E`<sub>*N*</sub>`))` for a unary right fold,
- `(((``E` *op* `E`<sub>1</sub>`)` *op* `E`<sub>2</sub>`)` *op* …`)` *op* `E`<sub>*N*</sub> for a binary left fold, and
- `E`<sub>1</sub> *op* `(`… *op* `(``E`<sub>*N*-1</sub> *op* `(``E`<sub>*N*</sub> *op* `E``)))` for a binary right fold.

In each case, *op* is the *fold-operator*, *N* is the number of elements in the pack expansion parameters, and each `E`<sub>*i*</sub> is generated by instantiating the pattern and replacing each pack expansion parameter with its *i*<sup></sup>*th* element. For a binary fold-expression, `E` is generated by instantiating the *cast-expression* that did not contain an unexpanded pack. [*Example 7*:

```cpp
template<typename ...Args>
  bool all(Args ...args) { return (... && args); }

bool b = all(true, true, true, false);
```

Within the instantiation of `all`, the returned expression expands to `((true && true) && true) && false`, which evaluates to `false`. — *end example*] If *N* is zero for a unary fold-expression, the value of the expression is shown in [Table 17](temp.variadic.md#tab:temp.fold.empty); if the operator is not listed in [Table 17](temp.variadic.md#tab:temp.fold.empty), the instantiation is ill-formed.

<a id="tab:temp.fold.empty"></a>
**Table 17: Value of folding empty sequences** [tab:temp.fold.empty]

| Operator | Value when pack is empty |
| --- | --- |
| `&&` | `true` |
| `\|\|` | `false` |
| `,` | `void()` |
