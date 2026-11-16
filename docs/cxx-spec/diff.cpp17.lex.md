# C.1.1 [[lex]](lex.md): lexical conventions [diff.cpp17.lex]

[Contents](README.md) · up: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · prev: [C.1 C++ and ISO C++ 2017 \[diff.cpp17\]](diff.cpp17.md) · next: [C.1.2 [[basic]](basic.md): basics \[diff.cpp17.basic\]](diff.cpp17.basic.md)

**1** **Affected subclauses:** [[lex.pptoken]](lex.pptoken.md), [[module.unit]](module.unit.md), [[module.import]](module.import.md), [[cpp.pre]](cpp.pre.md), [[cpp.module]](cpp.module.md), [[cpp.import]](cpp.import.md) <br>**Change:** New identifiers with special meaning. <br>**Rationale:** Required for new features. <br>**Effect on original feature:** Logical lines beginning with `module` or `import` may be interpreted differently in this International Standard. [*Example 1*:

```cpp
class module {};
module m1;          // was variable declaration; now module-declaration
module *m2;         // variable declaration

class import {};
import j1;          // was variable declaration; now import-declaration
::import j2;        // variable declaration
```

— *end example*]

**2** **Affected subclause:** [[lex.header]](lex.header.md) <br>**Change:** *header-name* tokens are formed in more contexts. <br>**Rationale:** Required for new features. <br>**Effect on original feature:** When the identifier `import` is followed by a `<` character, a *header-name* token may be formed. [*Example 2*:

```cpp
template<typename> class import {};
import<int> f();                // ill-formed; previously well-formed
::import<int> g();              // OK
```

— *end example*]

**3** **Affected subclause:** [[lex.key]](lex.key.md) <br>**Change:** New keywords. <br>**Rationale:** Required for new features.

- The `char8_t` keyword is added to differentiate the types of ordinary and UTF-8 literals ([[lex.string]](lex.string.md)).
- The `concept` keyword is added to enable the definition of concepts ([[temp.concept]](temp.concept.md)).
- The `consteval` keyword is added to declare immediate functions ([[dcl.constexpr]](dcl.constexpr.md)).
- The `constinit` keyword is added to prevent unintended dynamic initialization ([[dcl.constinit]](dcl.constinit.md)).
- The `co_await`, `co_yield`, and `co_return` keywords are added to enable the definition of coroutines ([[dcl.fct.def.coroutine]](dcl.fct.def.coroutine.md)).
- The `requires` keyword is added to introduce constraints through a *requires-clause* ([[temp.pre]](temp.pre.md)) or a *requires-expression* ([[expr.prim.req]](expr.prim.req.md)).

**Effect on original feature:** Valid C++ 2017 code using `char8_t`, `concept`, `consteval`, `constinit`, `co_await`, `co_yield`, `co_return`, or `requires` as an identifier is not valid in this International Standard.

**4** **Affected subclause:** [[lex.operators]](lex.operators.md) <br>**Change:** New operator `<=>`. <br>**Rationale:** Necessary for new functionality. <br>**Effect on original feature:** Valid C++ 2017 code that contains a `<=` token immediately followed by a `>` token may be ill-formed or have different semantics in this International Standard:

```cpp
namespace N {
  struct X {};
  bool operator<=(X, X);
  template<bool(X, X)> struct Y {};
  Y<operator<=> y;              // ill-formed; previously well-formed
}
```

**5** **Affected subclause:** [[lex.literal]](lex.literal.md) <br>**Change:** Type of UTF-8 string and character literals. <br>**Rationale:** Required for new features. The changed types enable function overloading, template specialization, and type deduction to distinguish ordinary and UTF-8 string and character literals. <br>**Effect on original feature:** Valid C++ 2017 code that depends on UTF-8 string literals having type “array of `const char`” and UTF-8 character literals having type “`char`” is not valid in this International Standard.

```cpp
const auto *u8s = u8"text";     // u8s previously deduced as const char*; now deduced as const char8_t*
const char *ps = u8s;           // ill-formed; previously well-formed

auto u8c = u8'c';               // u8c previously deduced as char; now deduced as char8_t
char *pc = &u8c;                // ill-formed; previously well-formed

std::string s = u8"text";       // ill-formed; previously well-formed

void f(const char *s);
f(u8"text");                    // ill-formed; previously well-formed

template<typename> struct ct;
template<> struct ct<char> {
  using type = char;
};
ct<decltype(u8'c')>::type x;    // ill-formed; previously well-formed.
```
