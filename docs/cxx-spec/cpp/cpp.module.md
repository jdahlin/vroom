# 15.4 Module directive [cpp.module]

[Contents](../README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15.3 Source file inclusion \[cpp.include\]](cpp.include.md) · next: [15.5 Header unit importation \[cpp.import\]](cpp.import.md)

```bnf
pp-module:
    'export'ₒₚₜ 'module' pp-tokensₒₚₜ ';' new-line
```

**1** A *pp-module* shall not appear in a context where `module` or (if it is the first token of the *pp-module*) `export` is an identifier defined as an object-like macro.

**2** Any preprocessing tokens after the `module` preprocessing token in the `module` directive are processed just as in normal text. [*Note 1*: Each identifier currently defined as a macro name is replaced by its replacement list of preprocessing tokens. — *end note*]

**3** The `module` and `export` (if it exists) preprocessing tokens are replaced by the *module-keyword* and *export-keyword* preprocessing tokens respectively. [*Note 2*: This makes the line no longer a directive so it is not removed at the end of phase 4. — *end note*]
