# 8.6.1 The `while` statement [stmt.while]

[Contents](README.md) · up: [8.6 Iteration statements \[stmt.iter\]](stmt.iter.md) · prev: [8.6 Iteration statements \[stmt.iter\]](stmt.iter.md) · next: [8.6.2 The `do` statement \[stmt.do\]](stmt.do.md)

**1** In the `while` statement the substatement is executed repeatedly until the value of the condition ([[stmt.select]](stmt.select.md)) becomes `false`. The test takes place before each execution of the substatement.

**2** When the condition of a `while` statement is a declaration, the scope of the variable that is declared extends from its point of declaration ([[basic.scope.pdecl]](basic.scope.pdecl.md)) to the end of the `while` *statement*. A `while` statement is equivalent to

```bnf
label ':'
'{'
    'if' '(' condition ')' '{'
        statement
        'goto' label ';'
    '}'
'}'
```

[*Note 1*: The variable created in the condition is destroyed and created with each iteration of the loop. [*Example 1*:

```cpp
struct A {
  int val;
  A(int i) : val(i) { }
  ~A() { }
  operator bool() { return val != 0; }
};
int i = 1;
while (A a = i) {
  // ...
  i = 0;
}
```

In the while-loop, the constructor and destructor are each called twice, once for the condition that succeeds and once for the condition that fails. — *end example*] — *end note*]
