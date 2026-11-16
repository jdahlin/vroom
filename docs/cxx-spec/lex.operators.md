# 5.12 Operators and punctuators [lex.operators]

[Contents](README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.11 Keywords \[lex.key\]](lex.key.md) · next: [5.13 Literals \[lex.literal\]](lex.literal.md)

**1** The lexical representation of C++ programs includes a number of preprocessing tokens that are used in the syntax of the preprocessor or are converted into tokens for operators and punctuators:

```bnf
preprocessing-op-or-punc:
    preprocessing-operator
    operator-or-punctuator
```

```bnf
preprocessing-operator: one of
    '# ## %: %:%:'
```

```bnf
operator-or-punctuator: one of
    '{ } [ ] ( )'
    '<: :> <% %>;: ...'
    '? ::. .* -> ->* ~'
    '! + - * / % ^ & |'
    '= += -= *= /= %= ^= &= |='
    '== != < > <= >= <=> && ||'
    '<< >> <<= >>= ++ --,'
    'and or xor not bitand bitor compl'
    'and_eq or_eq xor_eq not_eq'
```

Each *operator-or-punctuator* is converted to a single token in translation phase 7 ([[lex.phases]](lex.phases.md)).
