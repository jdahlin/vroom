# 15.9 Pragma directive [cpp.pragma]

[Contents](../README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15.8 Error directive \[cpp.error\]](cpp.error.md) · next: [15.10 Null directive \[cpp.null\]](cpp.null.md)

**1** A preprocessing directive of the form

```bnf
'# pragma' pp-tokensₒₚₜ new-line
```

causes the implementation to behave in an implementation-defined manner. The behavior might cause translation to fail or cause the translator or the resulting program to behave in a non-conforming manner. Any pragma that is not recognized by the implementation is ignored.
