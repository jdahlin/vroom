# 6.5.4 Elaborated type specifiers [basic.lookup.elab]

[Contents](../README.md) · up: [6.5 Name lookup \[basic.lookup\]](basic.lookup.md) · prev: [6.5.3.2 Namespace members \[namespace.qual\]](namespace.qual.md) · next: [6.5.5 Class member access \[basic.lookup.classref\]](basic.lookup.classref.md)

**1** An *elaborated-type-specifier* ([[dcl.type.elab]](../dcl/dcl.type.elab.md)) may be used to refer to a previously declared *class-name* or *enum-name* even though the name has been hidden by a non-type declaration ([[basic.scope.hiding]](basic.scope.hiding.md)).

**2** If the *elaborated-type-specifier* has no *nested-name-specifier*, and unless the *elaborated-type-specifier* appears in a declaration with the following form:

```bnf
class-key attribute-specifier-seqₒₚₜ identifier ';'
```

the *identifier* is looked up according to [[basic.lookup.unqual]](basic.lookup.unqual.md) but ignoring any non-type names that have been declared. If the *elaborated-type-specifier* is introduced by the `enum` keyword and this lookup does not find a previously declared *type-name*, the *elaborated-type-specifier* is ill-formed. If the *elaborated-type-specifier* is introduced by the *class-key* and this lookup does not find a previously declared *type-name*, or if the *elaborated-type-specifier* appears in a declaration with the form:

```bnf
class-key attribute-specifier-seqₒₚₜ identifier ';'
```

the *elaborated-type-specifier* is a declaration that introduces the *class-name* as described in [[basic.scope.pdecl]](basic.scope.pdecl.md).

**3** If the *elaborated-type-specifier* has a *nested-name-specifier*, qualified name lookup is performed, as described in [[basic.lookup.qual]](basic.lookup.qual.md), but ignoring any non-type names that have been declared. If the name lookup does not find a previously declared *type-name*, the *elaborated-type-specifier* is ill-formed.

**4** [*Example 1*:

```cpp
struct Node {
  struct Node* Next;            // OK: Refers to injected-class-name Node
  struct Data* Data;            // OK: Declares type Data at global scope and member Data
};

struct Data {
  struct Node* Node;            // OK: Refers to Node at global scope
  friend struct ::Glob;         // error: Glob is not declared, cannot introduce a qualified type ([dcl.type.elab])
  friend struct Glob;           // OK: Refers to (as yet) undeclared Glob at global scope.
  /* ... */
};

struct Base {
  struct Data;                  // OK: Declares nested Data
  struct ::Data*     thatData;  // OK: Refers to ::Data
  struct Base::Data* thisData;  // OK: Refers to nested Data
  friend class ::Data;          // OK: global Data is a friend
  friend class Data;            // OK: nested Data is a friend
  struct Data { /* ... */ };    // Defines nested Data
};

struct Data;                    // OK: Redeclares Data at global scope
struct ::Data;                  // error: cannot introduce a qualified type ([dcl.type.elab])
struct Base::Data;              // error: cannot introduce a qualified type ([dcl.type.elab])
struct Base::Datum;             // error: Datum undefined
struct Base::Data* pBase;       // OK: refers to nested Data
```

— *end example*]
