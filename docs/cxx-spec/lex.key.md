# 5.11 Keywords [lex.key]

[Contents](README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.10 Identifiers \[lex.name\]](lex.name.md) · next: [5.12 Operators and punctuators \[lex.operators\]](lex.operators.md)

```bnf
keyword:
    any identifier listed in Table 5
    import-keyword
    module-keyword
    export-keyword
```

**1** The identifiers shown in [Table 5](lex.key.md#tab:lex.key) are reserved for use as keywords (that is, they are unconditionally treated as keywords in phase 7) except in an *attribute-token* ([[dcl.attr.grammar]](dcl.attr.grammar.md)). [*Note 1*: The `register` keyword is unused but is reserved for future use. — *end note*]

<a id="tab:lex.key"></a>
**Table 5: Keywords** [tab:lex.key]

|  |
| --- |
| `alignas` |
| `alignof` |
| `asm` |
| `auto` |
| `bool` |
| `break` |
| `case` |
| `catch` |
| `char` |
| `char8_t` |
| `char16_t` |
| `char32_t` |
| `class` |
| `concept` |
| `const` |
| `consteval` |
| `constexpr` |
| `constinit` |
| `const_cast` |
| `continue` |
| `co_await` |
| `co_return` |
| `co_yield` |
| `decltype` |
| `default` |
| `delete` |
| `do` |
| `double` |
| `dynamic_cast` |
| `else` |
| `enum` |
| `explicit` |
| `export` |
| `extern` |
| `false` |
| `float` |
| `for` |
| `friend` |
| `goto` |
| `if` |
| `inline` |
| `int` |
| `long` |
| `mutable` |
| `namespace` |
| `new` |
| `noexcept` |
| `nullptr` |
| `operator` |
| `private` |
| `protected` |
| `public` |
| `register` |
| `reinterpret_cast` |
| `requires` |
| `return` |
| `short` |
| `signed` |
| `sizeof` |
| `static` |
| `static_assert` |
| `static_cast` |
| `struct` |
| `switch` |
| `template` |
| `this` |
| `thread_local` |
| `throw` |
| `true` |
| `try` |
| `typedef` |
| `typeid` |
| `typename` |
| `union` |
| `unsigned` |
| `using` |
| `virtual` |
| `void` |
| `volatile` |
| `wchar_t` |
| `while` |

**2** Furthermore, the alternative representations shown in [Table 6](lex.key.md#tab:lex.key.digraph) for certain operators and punctuators ([[lex.digraph]](lex.digraph.md)) are reserved and shall not be used otherwise.

<a id="tab:lex.key.digraph"></a>
**Table 6: Alternative representations** [tab:lex.key.digraph]

|  |  |  |  |  |  |
| --- | --- | --- | --- | --- | --- |
| `and` | `and_eq` | `bitand` | `bitor` | `compl` | `not` |
| `not_eq` | `or` | `or_eq` | `xor` | `xor_eq` |  |
