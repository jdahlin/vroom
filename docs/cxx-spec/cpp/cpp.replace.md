# 15.6 Macro replacement [cpp.replace]

[Contents](../README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15.5 Header unit importation \[cpp.import\]](cpp.import.md) · next: [15.6.1 Argument substitution \[cpp.subst\]](cpp.subst.md)

**1** Two replacement lists are identical if and only if the preprocessing tokens in both have the same number, ordering, spelling, and white-space separation, where all white-space separations are considered identical.

**2** An identifier currently defined as an object-like macro (see below) may be redefined by another `#define` preprocessing directive provided that the second definition is an object-like macro definition and the two replacement lists are identical, otherwise the program is ill-formed. Likewise, an identifier currently defined as a function-like macro (see below) may be redefined by another `#define` preprocessing directive provided that the second definition is a function-like macro definition that has the same number and spelling of parameters, and the two replacement lists are identical, otherwise the program is ill-formed.

**3** [*Example 1*: The following sequence is valid:

```cpp
#define OBJ_LIKE      (1-1)
#define OBJ_LIKE      /* white space */ (1-1) /* other */
#define FUNC_LIKE(a)   ( a )
#define FUNC_LIKE( a )(     /* note the white space */ \
                a /* other stuff on this line
                  */ )
```

But the following redefinitions are invalid:

```cpp
#define OBJ_LIKE    (0)         // different token sequence
#define OBJ_LIKE    (1 - 1)     // different white space
#define FUNC_LIKE(b) ( a )      // different parameter usage
#define FUNC_LIKE(b) ( b )      // different parameter spelling
```

— *end example*]

**4** There shall be white-space between the identifier and the replacement list in the definition of an object-like macro.

**5** If the *identifier-list* in the macro definition does not end with an ellipsis, the number of arguments (including those arguments consisting of no preprocessing tokens) in an invocation of a function-like macro shall equal the number of parameters in the macro definition. Otherwise, there shall be at least as many arguments in the invocation as there are parameters in the macro definition (excluding the `...`). There shall exist a `)` preprocessing token that terminates the invocation.

**6** The identifiers `__VA_ARGS__` and `__VA_OPT__` shall occur only in the *replacement-list* of a function-like macro that uses the ellipsis notation in the parameters.

**7** A parameter identifier in a function-like macro shall be uniquely declared within its scope.

**8** The identifier immediately following the `define` is called the *macro name*. There is one name space for macro names. Any white-space characters preceding or following the replacement list of preprocessing tokens are not considered part of the replacement list for either form of macro.

**9** If a `#` preprocessing token, followed by an identifier, occurs lexically at the point at which a preprocessing directive could begin, the identifier is not subject to macro replacement.

**10** A preprocessing directive of the form

```bnf
'# define' identifier replacement-list new-line
```

defines an *object-like macro* that causes each subsequent instance of the macro name[^1] to be replaced by the replacement list of preprocessing tokens that constitute the remainder of the directive.[^2] The replacement list is then rescanned for more macro names as specified below.

**11** [*Example 2*: The simplest use of this facility is to define a “manifest constant”, as in

```cpp
#define TABSIZE 100
int table[TABSIZE];
```

— *end example*]

**12** A preprocessing directive of the form

```bnf
'# define' identifier lparen identifier-listₒₚₜ ')' replacement-list new-line
'# define' identifier lparen '...' ')' replacement-list new-line
'# define' identifier lparen identifier-list ', ...' ')' replacement-list new-line
```

defines a *function-like macro* with parameters, whose use is similar syntactically to a function call. The parameters are specified by the optional list of identifiers, whose scope extends from their declaration in the identifier list until the new-line character that terminates the `#define` preprocessing directive. Each subsequent instance of the function-like macro name followed by a `(` as the next preprocessing token introduces the sequence of preprocessing tokens that is replaced by the replacement list in the definition (an invocation of the macro). The replaced sequence of preprocessing tokens is terminated by the matching `)` preprocessing token, skipping intervening matched pairs of left and right parenthesis preprocessing tokens. Within the sequence of preprocessing tokens making up an invocation of a function-like macro, new-line is considered a normal white-space character.

**13** The sequence of preprocessing tokens bounded by the outside-most matching parentheses forms the list of arguments for the function-like macro. The individual arguments within the list are separated by comma preprocessing tokens, but comma preprocessing tokens between matching inner parentheses do not separate arguments. If there are sequences of preprocessing tokens within the list of arguments that would otherwise act as preprocessing directives,[^3] the behavior is undefined.

**14** [*Example 3*: The following defines a function-like macro whose value is the maximum of its arguments. It has the disadvantages of evaluating one or the other of its arguments a second time (including side effects) and generating more code than a function if invoked several times. It also cannot have its address taken, as it has none.

```cpp
#define max(a, b) ((a) > (b) ? (a) : (b))
```

The parentheses ensure that the arguments and the resulting expression are bound properly. — *end example*]

**15** If there is a `...` immediately preceding the `)` in the function-like macro definition, then the trailing arguments (if any), including any separating comma preprocessing tokens, are merged to form a single item: the *variable arguments*. The number of arguments so combined is such that, following merger, the number of arguments is either equal to or one more than the number of parameters in the macro definition (excluding the `...`).

## Subclauses

- [15.6.1 Argument substitution \[cpp.subst\]](cpp.subst.md)
- [15.6.2 The `#` operator \[cpp.stringize\]](cpp.stringize.md)
- [15.6.3 The `##` operator \[cpp.concat\]](cpp.concat.md)
- [15.6.4 Rescanning and further replacement \[cpp.rescan\]](cpp.rescan.md)
- [15.6.5 Scope of macro definitions \[cpp.scope\]](cpp.scope.md)

[^1]: Since, by macro-replacement time, all *character-literal*s and *string-literal*s are preprocessing tokens, not sequences possibly containing identifier-like subsequences (see [[lex.phases]](../lex/lex.phases.md), translation phases), they are never scanned for macro names or parameters.
[^2]: An alternative token ([[lex.digraph]](../lex/lex.digraph.md)) is not an identifier, even when its spelling consists entirely of letters and underscores. Therefore it is not possible to define a macro whose name is the same as that of an alternative token.
[^3]: A *conditionally-supported-directive* is a preprocessing directive regardless of whether the implementation supports it.
