# 8.6.3 The `for` statement [stmt.for]

[Contents](../README.md) · up: [8.6 Iteration statements \[stmt.iter\]](stmt.iter.md) · prev: [8.6.2 The `do` statement \[stmt.do\]](stmt.do.md) · next: [8.6.4 The range-based `for` statement \[stmt.ranged\]](stmt.ranged.md)

**1** The `for` statement

```bnf
'for' '(' init-statement conditionₒₚₜ ';' expressionₒₚₜ ')' statement
```

is equivalent to

```bnf
'{'
    init-statement
    'while' '(' condition ')' '{'
        statement
        expression ';'
    '}'
'}'
```

except that names declared in the *init-statement* are in the same declarative region as those declared in the *condition*, and except that a `continue` in *statement* (not enclosed in another iteration statement) will execute *expression* before re-evaluating *condition*. [*Note 1*: Thus the first statement specifies initialization for the loop; the condition ([[stmt.select]](stmt.select.md)) specifies a test, sequenced before each iteration, such that the loop is exited when the condition becomes `false`; the expression often specifies incrementing that is sequenced after each iteration. — *end note*]

**2** Either or both of the *condition* and the *expression* can be omitted. A missing *condition* makes the implied `while` clause equivalent to `while(true)`.

**3** If the *init-statement* is a declaration, the scope of the name(s) declared extends to the end of the `for` statement. [*Example 1*:

```cpp
int i = 42;
int a[10];

for (int i = 0; i < 10; i++)
  a[i] = i;

int j = i;          // j = 42
```

— *end example*]
