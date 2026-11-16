# 15.12 Pragma operator [cpp.pragma.op]

[Contents](../README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15.11 Predefined macro names \[cpp.predefined\]](cpp.predefined.md) · next: [16 Library introduction \[library\]](../lib/library/library.md)

**1** A unary operator expression of the form:

```bnf
'_Pragma' '(' string-literal ')'
```

is processed as follows: The *string-literal* is *destringized* by deleting the `L` prefix, if present, deleting the leading and trailing double-quotes, replacing each escape sequence `\"` by a double-quote, and replacing each escape sequence `\\` by a single backslash. The resulting sequence of characters is processed through translation phase 3 to produce preprocessing tokens that are executed as if they were the *pp-tokens* in a pragma directive. The original four preprocessing tokens in the unary operator expression are removed.

**2** [*Example 1*:

```cpp
#pragma listing on "..\listing.dir"
```

can also be expressed as:

```cpp
_Pragma ( "listing on \"..\\listing.dir\"" )
```

The latter form is processed in the same way whether it appears literally as shown, or results from macro replacement, as in:

```cpp
#define LISTING(x) PRAGMA(listing on #x)
#define PRAGMA(x) _Pragma(#x)

LISTING( ..\listing.dir )
```

— *end example*]
