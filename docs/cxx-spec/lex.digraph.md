# 5.5 Alternative tokens [lex.digraph]

[Contents](README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.4 Preprocessing tokens \[lex.pptoken\]](lex.pptoken.md) · next: [5.6 Tokens \[lex.token\]](lex.token.md)

**1** Alternative token representations are provided for some operators and punctuators.[^1]

**2** In all respects of the language, each alternative token behaves the same, respectively, as its primary token, except for its spelling.[^2] The set of alternative tokens is defined in [Table 1](lex.digraph.md#tab:lex.digraph).

<a id="tab:lex.digraph"></a>
**Table 1: Alternative tokens** [tab:lex.digraph]

| Alternative | Primary |  |  |  |  |
| --- | --- | --- | --- | --- | --- |
| `<%` | `{` | `and` | `&&` | `and_eq` | `&=` |
| `%>` | `}` | `bitor` | `\|` | `or_eq` | `\|=` |
| `<:` | `[` | `or` | `\|\|` | `xor_eq` | `^=` |
| `:>` | `]` | `xor` | `^` | `not` | `!` |
| `%:` | `#` | `compl` | `~` | `not_eq` | `!=` |
| `%:%:` | `##` | `bitand` | `&` |  |  |

[^1]: These include “digraphs” and additional reserved words. The term “digraph” (token consisting of two characters) is not perfectly descriptive, since one of the alternative *preprocessing-token*s is `%:%:` and of course several primary tokens contain two characters. Nonetheless, those alternative tokens that aren’t lexical keywords are colloquially known as “digraphs”.
[^2]: Thus the “stringized” values ([[cpp.stringize]](cpp.stringize.md)) of `[` and `<:` will be different, maintaining the source spelling, but the tokens can otherwise be freely interchanged.
