# 15.6.5 Scope of macro definitions [cpp.scope]

[Contents](../README.md) · up: [15.6 Macro replacement \[cpp.replace\]](cpp.replace.md) · prev: [15.6.4 Rescanning and further replacement \[cpp.rescan\]](cpp.rescan.md) · next: [15.7 Line control \[cpp.line\]](cpp.line.md)

**1** A macro definition lasts (independent of block structure) until a corresponding `#undef` directive is encountered or (if none is encountered) until the end of the translation unit. Macro definitions have no significance after translation phase 4.

**2** A preprocessing directive of the form

```bnf
'# undef' identifier new-line
```

causes the specified identifier no longer to be defined as a macro name. It is ignored if the specified identifier is not currently defined as a macro name.
