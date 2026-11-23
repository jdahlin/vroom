# A.9 Overloading [gram.over]

[Contents](../README.md) · up: [Annex A Grammar summary \[gram\]](gram.md) · prev: [A.8 Classes \[gram.class\]](gram.class.md) · next: [A.10 Templates \[gram.temp\]](gram.temp.md)

```bnf
operator-function-id:
    'operator' operator
```

```bnf
operator: one of
    'new delete new[] delete[] co_await () [] -> ->*'
    '~ ! + - * / % ^ &'
    '| = += -= *= /= %= ^= &='
    '|= == != < > <= >= <=> &&'
    '|| << >> <<= >>= ++ --,'
```

```bnf
literal-operator-id:
    'operator' string-literal identifier
    'operator' user-defined-string-literal
```
