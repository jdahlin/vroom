# 15.8 Error directive [cpp.error]

[Contents](../README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15.7 Line control \[cpp.line\]](cpp.line.md) · next: [15.9 Pragma directive \[cpp.pragma\]](cpp.pragma.md)

**1** A preprocessing directive of the form

```bnf
'# error' pp-tokensₒₚₜ new-line
```

causes the implementation to produce a diagnostic message that includes the specified sequence of preprocessing tokens, and renders the program ill-formed.
