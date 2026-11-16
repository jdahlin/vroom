# 16.3 Definitions [definitions]

[Contents](../../README.md) · up: [16 Library introduction \[library\]](library.md) · prev: [16.2 The C standard library \[library.c\]](library.c.md) · next: [16.4 Method of description \[description\]](description.md)

**1** [*Note 1*: [[intro.defs]](../../intro/intro.defs.md) defines additional terms used elsewhere in this document. — *end note*]

<a id="defns.arbitrary.stream"></a>
### 16.3.1 arbitrary-positional stream [defns.arbitrary.stream]

stream (described in [[input.output]](../io/input.output.md)) that can seek to any integral position within the length of the stream

[*Note 1 to entry*: Every arbitrary-positional stream is also a repositional stream. — *end note*]

<a id="defns.character"></a>
### 16.3.2 character [defns.character]

⟨[[strings]](../strings/strings.md), [[localization]](../localization/localization.md), [[input.output]](../io/input.output.md), and [[re]](../re/re.md)⟩ object which, when treated sequentially, can represent text

[*Note 1 to entry*: The term does not mean only `char`, `char8_t`, `char16_t`, `char32_t`, and `wchar_t` objects, but any value that can be represented by a type that provides the definitions specified in these Clauses. — *end note*]

<a id="defns.character.container"></a>
### 16.3.3 character container type [defns.character.container]

class or a type used to represent a character

[*Note 1 to entry*: It is used for one of the template parameters of the string, iostream, and regular expression class templates. — *end note*]

<a id="defns.comparison"></a>
### 16.3.4 comparison function [defns.comparison]

operator function ([[over.oper]](../../over/over.oper.md)) for any of the equality ([[expr.eq]](../../expr/expr.eq.md)), relational ([[expr.rel]](../../expr/expr.rel.md)), or three-way comparison ([[expr.spaceship]](../../expr/expr.spaceship.md)) operators

<a id="defns.component"></a>
### 16.3.5 component [defns.component]

group of library entities directly related as members, parameters, or return types

[*Note 1 to entry*: For example, the class template `basic_string` and the non-member function templates that operate on strings are referred to as the *string component*. — *end note*]

<a id="defns.const.subexpr"></a>
### 16.3.6 constant subexpression [defns.const.subexpr]

expression whose evaluation as subexpression of a *conditional-expression* `CE` ([[expr.cond]](../../expr/expr.cond.md)) would not prevent `CE` from being a core constant expression ([[expr.const]](../../expr/expr.const.md))

<a id="defns.deadlock"></a>
### 16.3.7 deadlock [defns.deadlock]

situation wherein one or more threads are unable to continue execution because each is blocked waiting for one or more of the others to satisfy some condition

<a id="defns.default.behavior.impl"></a>
### 16.3.8 default behavior [defns.default.behavior.impl]

⟨implementation⟩ specific behavior provided by the implementation, within the scope of the required behavior

<a id="defns.default.behavior.func"></a>
### 16.3.9 default behavior [defns.default.behavior.func]

⟨specification⟩ description of replacement function and handler function semantics

<a id="defns.direct-non-list-init"></a>
### 16.3.10 direct-non-list-initialization [defns.direct-non-list-init]

direct-initialization ([[dcl.init]](../../dcl/dcl.init.md)) that is not list-initialization ([[dcl.init.list]](../../dcl/dcl.init.list.md))

<a id="defns.expression-equivalent"></a>
### 16.3.11 expression-equivalent [defns.expression-equivalent]

expressions that all have the same effects, either are all potentially-throwing ([[except.spec]](../../except/except.spec.md)) or are all not potentially-throwing, and either are all constant subexpressions or are all not constant subexpressions

[*Example 1*: For a value `x` of type `int` and a function `f` that accepts integer arguments, the expressions `f(x + 2)`, `f(2 + x)`, and `f(1 + x + 1)` are expression-equivalent. — *end example*]

<a id="defns.handler"></a>
### 16.3.12 handler function [defns.handler]

non-reserved function whose definition may be provided by a C++ program

[*Note 1 to entry*: A C++ program may designate a handler function at various points in its execution by supplying a pointer to the function when calling any of the library functions that install handler functions ([[support]](../support/support.md)). — *end note*]

<a id="defns.order.ptr"></a>
### 16.3.13 implementation-defined strict total order over pointers [defns.order.ptr]

implementation-defined strict total ordering over all pointer values such that the ordering is consistent with the partial order imposed by the builtin operators `<`, `>`, `<=`, `>=`, and `<=>`

<a id="defns.iostream.templates"></a>
### 16.3.14 iostream class templates [defns.iostream.templates]

templates, defined in [[input.output]](../io/input.output.md), that take two template arguments

[*Note 1 to entry*: The arguments are named `charT` and `traits`. The argument `charT` is a character container class, and the argument `traits` is a class which defines additional characteristics and functions of the character type represented by `charT` necessary to implement the iostream class templates. — *end note*]

<a id="defns.modifier"></a>
### 16.3.15 modifier function [defns.modifier]

class member function ([[class.mfct]](../../class/class.mfct.md)) other than a constructor, assignment operator, or destructor that alters the state of an object of the class

<a id="defns.move.assign"></a>
### 16.3.16 move assignment [defns.move.assign]

assignment of an rvalue of some object type to a modifiable lvalue of the same type

<a id="defns.move.constr"></a>
### 16.3.17 move construction [defns.move.constr]

direct-initialization of an object of some type with an rvalue of the same type

<a id="defns.ntcts"></a>
### 16.3.18 NTCTS [defns.ntcts]

sequence of values that have character type that precede the terminating null character type value `charT()`

<a id="defns.observer"></a>
### 16.3.19 observer function [defns.observer]

class member function ([[class.mfct]](../../class/class.mfct.md)) that accesses the state of an object of the class but does not alter that state

[*Note 1 to entry*: Observer functions are specified as `const` member functions ([[class.this]](../../class/class.this.md)). — *end note*]

<a id="defns.prog.def.spec"></a>
### 16.3.20 program-defined specialization [defns.prog.def.spec]

explicit template specialization or partial specialization that is not part of the C++ standard library and not defined by the implementation

<a id="defns.prog.def.type"></a>
### 16.3.21 program-defined type [defns.prog.def.type]

non-closure class type or enumeration type that is not part of the C++ standard library and not defined by the implementation, or a closure type of a non-implementation-provided lambda expression, or an instantiation of a program-defined specialization

[*Note 1 to entry*: Types defined by the implementation include extensions ([[intro.compliance]](../../intro/intro.compliance.md)) and internal types used by the library. — *end note*]

<a id="defns.projection"></a>
### 16.3.22 projection [defns.projection]

⟨function object argument⟩ transformation that an algorithm applies before inspecting the values of elements

[*Example 1*:

```cpp
std::pair<int, std::string_view> pairs[] = {{2, "foo"}, {1, "bar"}, {0, "baz"}};
std::ranges::sort(pairs, std::ranges::less{}, [](auto const& p) { return p.first; });
```

sorts the pairs in increasing order of their `first` members:

```cpp
{{0, "baz"}, {1, "bar"}, {2, "foo"}}
```

— *end example*]

<a id="defns.referenceable"></a>
### 16.3.23 referenceable type [defns.referenceable]

type that is either an object type, a function type that does not have cv-qualifiers or a *ref-qualifier*, or a reference type

[*Note 1 to entry*: The term describes a type to which a reference can be created, including reference types. — *end note*]

<a id="defns.replacement"></a>
### 16.3.24 replacement function [defns.replacement]

non-reserved function whose definition is provided by a C++ program

[*Note 1 to entry*: Only one definition for such a function is in effect for the duration of the program’s execution, as the result of creating the program ([[lex.phases]](../../lex/lex.phases.md)) and resolving the definitions of all translation units ([[basic.link]](../../basic/basic.link.md)). — *end note*]

<a id="defns.repositional.stream"></a>
### 16.3.25 repositional stream [defns.repositional.stream]

stream (described in [[input.output]](../io/input.output.md)) that can seek to a position that was previously encountered

<a id="defns.required.behavior"></a>
### 16.3.26 required behavior [defns.required.behavior]

description of replacement function and handler function semantics applicable to both the behavior provided by the implementation and the behavior of any such function definition in the program

[*Note 1 to entry*: If such a function defined in a C++ program fails to meet the required behavior when it executes, the behavior is undefined. — *end note*]

<a id="defns.reserved.function"></a>
### 16.3.27 reserved function [defns.reserved.function]

function, specified as part of the C++ standard library, that is defined by the implementation

[*Note 1 to entry*: If a C++ program provides a definition for any reserved function, the results are undefined. — *end note*]

<a id="defns.stable"></a>
### 16.3.28 stable algorithm [defns.stable]

algorithm that preserves, as appropriate to the particular algorithm, the order of elements

[*Note 1 to entry*: Requirements for stable algorithms are given in [[algorithm.stable]](algorithm.stable.md). — *end note*]

<a id="defns.traits"></a>
### 16.3.29 traits class [defns.traits]

class that encapsulates a set of types and functions necessary for class templates and function templates to manipulate objects of types for which they are instantiated

<a id="defns.valid"></a>
### 16.3.30 valid but unspecified state [defns.valid]

value of an object that is not specified except that the object’s invariants are met and operations on the object behave as specified for its type

[*Example 1*: If an object `x` of type `std::vector<int>` is in a valid but unspecified state, `x.empty()` can be called unconditionally, and `x.front()` can be called only if `x.empty()` returns `false`. — *end example*]
