# 8.5.1 The `if` statement [stmt.if]

[Contents](README.md) · up: [8.5 Selection statements \[stmt.select\]](stmt.select.md) · prev: [8.5 Selection statements \[stmt.select\]](stmt.select.md) · next: [8.5.2 The `switch` statement \[stmt.switch\]](stmt.switch.md)

**1** If the condition ([[stmt.select]](stmt.select.md)) yields `true` the first substatement is executed. If the `else` part of the selection statement is present and the condition yields `false`, the second substatement is executed. If the first substatement is reached via a label, the condition is not evaluated and the second substatement is not executed. In the second form of `if` statement (the one including `else`), if the first substatement is also an `if` statement then that inner `if` statement shall contain an `else` part.[^1]

**2** If the `if` statement is of the form `if constexpr`, the value of the condition shall be a contextually converted constant expression of type `bool` ([[expr.const]](expr.const.md)); this form is called a *constexpr if* statement. If the value of the converted condition is `false`, the first substatement is a *discarded statement*, otherwise the second substatement, if present, is a discarded statement. During the instantiation of an enclosing templated entity ([[temp.pre]](temp.pre.md)), if the condition is not value-dependent after its instantiation, the discarded substatement (if any) is not instantiated. [*Note 1*: Odr-uses ([[basic.def.odr]](basic.def.odr.md)) in a discarded statement do not require an entity to be defined. — *end note*] A `case` or `default` label appearing within such an `if` statement shall be associated with a `switch` statement ([[stmt.switch]](stmt.switch.md)) within the same `if` statement. A label ([[stmt.label]](stmt.label.md)) declared in a substatement of a constexpr if statement shall only be referred to by a statement ([[stmt.goto]](stmt.goto.md)) in the same substatement. [*Example 1*:

```cpp
template<typename T, typename ... Rest> void g(T&& p, Rest&& ...rs) {
  // ... handle p

  if constexpr (sizeof...(rs) > 0)
    g(rs...);       // never instantiated with an empty argument list
}

extern int x;       // no definition of x required

int f() {
  if constexpr (true)
    return 0;
  else if (x)
    return x;
  else
    return -x;
}
```

— *end example*]

**3** An `if` statement of the form

```bnf
'if' 'constexpr'ₒₚₜ '(' init-statement condition ')' statement
```

is equivalent to

```bnf
'{'
    init-statement
    'if' 'constexpr'ₒₚₜ '(' condition ')' statement
'}'
```

and an `if` statement of the form

```bnf
'if' 'constexpr'ₒₚₜ '(' init-statement condition ')' statement 'else' statement
```

is equivalent to

```bnf
'{'
    init-statement
    'if' 'constexpr'ₒₚₜ '(' condition ')' statement 'else' statement
'}'
```

except that names declared in the *init-statement* are in the same declarative region as those declared in the *condition*.

[^1]: In other words, the `else` is associated with the nearest un-elsed `if`.
