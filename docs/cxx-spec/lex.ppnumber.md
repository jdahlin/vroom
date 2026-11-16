# 5.9 Preprocessing numbers [lex.ppnumber]

[Contents](README.md) · up: [5 Lexical conventions \[lex\]](lex.md) · prev: [5.8 Header names \[lex.header\]](lex.header.md) · next: [5.10 Identifiers \[lex.name\]](lex.name.md)

```bnf
pp-number:
    digit
    '.' digit
    pp-number digit
    pp-number identifier-nondigit
    pp-number ''' digit
    pp-number ''' nondigit
    pp-number 'e' sign
    pp-number 'E' sign
    pp-number 'p' sign
    pp-number 'P' sign
    pp-number '.'
```

**1** Preprocessing number tokens lexically include all *integer-literal* tokens ([[lex.icon]](lex.icon.md)) and all *floating-point-literal* tokens ([[lex.fcon]](lex.fcon.md)).

**2** A preprocessing number does not have a type or a value; it acquires both after a successful conversion to an *integer-literal* token or a *floating-point-literal* token.
