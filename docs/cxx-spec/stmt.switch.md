# 8.5.2 The `switch` statement [stmt.switch]

[Contents](README.md) · up: [8.5 Selection statements \[stmt.select\]](stmt.select.md) · prev: [8.5.1 The `if` statement \[stmt.if\]](stmt.if.md) · next: [8.6 Iteration statements \[stmt.iter\]](stmt.iter.md)

**1** The `switch` statement causes control to be transferred to one of several statements depending on the value of a condition.

**2** The condition shall be of integral type, enumeration type, or class type. If of class type, the condition is contextually implicitly converted ([[conv]](conv.md)) to an integral or enumeration type. If the (possibly converted) type is subject to integral promotions ([[conv.prom]](conv.prom.md)), the condition is converted to the promoted type. Any statement within the `switch` statement can be labeled with one or more case labels as follows:

```bnf
'case' constant-expression ':'
```

where the *constant-expression* shall be a converted constant expression ([[expr.const]](expr.const.md)) of the adjusted type of the switch condition. No two of the case constants in the same switch shall have the same value after conversion.

**3** There shall be at most one label of the form

```cpp
default :
```

within a `switch` statement.

**4** Switch statements can be nested; a `case` or `default` label is associated with the smallest switch enclosing it.

**5** When the `switch` statement is executed, its condition is evaluated. If one of the case constants has the same value as the condition, control is passed to the statement following the matched case label. If no case constant matches the condition, and if there is a `default` label, control passes to the statement labeled by the default label. If no case matches and if there is no `default` then none of the statements in the switch is executed.

**6** `case` and `default` labels in themselves do not alter the flow of control, which continues unimpeded across such labels. To exit from a switch, see `break`, [[stmt.break]](stmt.break.md). [*Note 1*: Usually, the substatement that is the subject of a switch is compound and `case` and `default` labels appear on the top-level statements contained within the (compound) substatement, but this is not required. Declarations can appear in the substatement of a `switch` statement. — *end note*]

**7** A `switch` statement of the form

```bnf
'switch' '(' init-statement condition ')' statement
```

is equivalent to

```bnf
'{'
    init-statement
    'switch' '(' condition ')' statement
'}'
```

except that names declared in the *init-statement* are in the same declarative region as those declared in the *condition*.
