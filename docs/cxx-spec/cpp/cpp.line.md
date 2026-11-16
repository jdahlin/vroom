# 15.7 Line control [cpp.line]

[Contents](../README.md) · up: [15 Preprocessing directives \[cpp\]](cpp.md) · prev: [15.6.5 Scope of macro definitions \[cpp.scope\]](cpp.scope.md) · next: [15.8 Error directive \[cpp.error\]](cpp.error.md)

**1** The *string-literal* of a `#line` directive, if present, shall be a character string literal.

**2** The *line number* of the current source line is one greater than the number of new-line characters read or introduced in translation phase 1 ([[lex.phases]](../lex/lex.phases.md)) while processing the source file to the current token.

**3** A preprocessing directive of the form

```bnf
'# line' digit-sequence new-line
```

causes the implementation to behave as if the following sequence of source lines begins with a source line that has a line number as specified by the digit sequence (interpreted as a decimal integer). If the digit sequence specifies zero or a number greater than 2147483647, the behavior is undefined.

**4** A preprocessing directive of the form

```bnf
'# line' digit-sequence '"' s-char-sequenceₒₚₜ '"' new-line
```

sets the presumed line number similarly and changes the presumed name of the source file to be the contents of the character string literal.

**5** A preprocessing directive of the form

```bnf
'# line' pp-tokens new-line
```

(that does not match one of the two previous forms) is permitted. The preprocessing tokens after `line` on the directive are processed just as in normal text (each identifier currently defined as a macro name is replaced by its replacement list of preprocessing tokens). If the directive resulting after all replacements does not match one of the two previous forms, the behavior is undefined; otherwise, the result is processed as appropriate.
