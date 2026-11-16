# C.5.2 [[basic]](basic.md): basics [diff.basic]

[Contents](README.md) · up: [C.5 C++ and ISO C \[diff.iso\]](diff.iso.md) · prev: [C.5.1 [[lex]](lex.md): lexical conventions \[diff.lex\]](diff.lex.md) · next: [C.5.3 [[expr]](expr.md): expressions \[diff.expr\]](diff.expr.md)

**1** **Affected subclause:** [[basic.def]](basic.def.md) <br>**Change:** C++ does not have “tentative definitions” as in C.<br> E.g., at file scope,

```cpp
int i;
int i;
```

is valid in C, invalid in C++. This makes it impossible to define mutually referential file-local objects with static storage duration, if initializers are restricted to the syntactic forms of C. For example,

```cpp
struct X { int i; struct X* next; };

static struct X a;
static struct X b = { 0, &a };
static struct X a = { 1, &b };
```

**Rationale:** This avoids having different initialization rules for fundamental types and user-defined types. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Semantic transformation. In C++, the initializer for one of a set of mutually-referential file-local objects with static storage duration must invoke a function call to achieve the initialization. <br>**How widely used:** Seldom.

**2** **Affected subclause:** [[basic.scope]](basic.scope.md) <br>**Change:** A `struct` is a scope in C++, not in C. <br>**Rationale:** Class scope is crucial to C++, and a struct is a class. <br>**Effect on original feature:** Change to semantics of well-defined feature. <br>**Difficulty of converting:** Semantic transformation. <br>**How widely used:** C programs use `struct` extremely frequently, but the change is only noticeable when `struct`, enumeration, or enumerator names are referred to outside the `struct`. The latter is probably rare.

**3** **Affected subclause:** [[basic.link]](basic.link.md) [also [[dcl.type]](dcl.type.md)] <br>**Change:** A name of file scope that is explicitly declared `const`, and not explicitly declared `extern`, has internal linkage, while in C it would have external linkage. <br>**Rationale:** Because const objects may be used as values during translation in C++, this feature urges programmers to provide an explicit initializer for each const object. This feature allows the user to put const objects in source files that are included in more than one translation unit. <br>**Effect on original feature:** Change to semantics of well-defined feature. <br>**Difficulty of converting:** Semantic transformation. <br>**How widely used:** Seldom.

**4** **Affected subclause:** [[basic.start.main]](basic.start.main.md) <br>**Change:** The `main` function cannot be called recursively and cannot have its address taken. <br>**Rationale:** The `main` function may require special actions. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Trivial: create an intermediary function such as `mymain(argc, argv)`. <br>**How widely used:** Seldom.

**5** **Affected subclause:** [[basic.types]](basic.types.md) <br>**Change:** C allows “compatible types” in several places, C++ does not.<br> For example, otherwise-identical `struct` types with different tag names are “compatible” in C but are distinctly different types in C++. <br>**Rationale:** Stricter type checking is essential for C++. <br>**Effect on original feature:** Deletion of semantically well-defined feature. <br>**Difficulty of converting:** Semantic transformation. The “typesafe linkage” mechanism will find many, but not all, of such problems. Those problems not found by typesafe linkage will continue to function properly, according to the “layout compatibility rules” of this document. <br>**How widely used:** Common.
