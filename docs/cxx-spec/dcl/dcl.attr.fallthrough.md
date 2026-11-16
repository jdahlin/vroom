# 9.12.5 Fallthrough attribute [dcl.attr.fallthrough]

[Contents](../README.md) · up: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · prev: [9.12.4 Deprecated attribute \[dcl.attr.deprecated\]](dcl.attr.deprecated.md) · next: [9.12.6 Likelihood attributes \[dcl.attr.likelihood\]](dcl.attr.likelihood.md)

**1** The *attribute-token* `fallthrough` may be applied to a null statement ([[stmt.expr]](../stmt/stmt.expr.md)); such a statement is a fallthrough statement. The *attribute-token* `fallthrough` shall appear at most once in each *attribute-list* and no *attribute-argument-clause* shall be present. A fallthrough statement may only appear within an enclosing `switch` statement ([[stmt.switch]](../stmt/stmt.switch.md)). The next statement that would be executed after a fallthrough statement shall be a labeled statement whose label is a case label or default label for the same `switch` statement and, if the fallthrough statement is contained in an iteration statement, the next statement shall be part of the same execution of the substatement of the innermost enclosing iteration statement. The program is ill-formed if there is no such statement.

**2** *Recommended practice:* The use of a fallthrough statement should suppress a warning that an implementation might otherwise issue for a case or default label that is reachable from another case or default label along some path of execution. Implementations should issue a warning if a fallthrough statement is not dynamically reachable.

**3** [*Example 1*:

```cpp
void f(int n) {
  void g(), h(), i();
  switch (n) {
  case 1:
  case 2:
    g();
    [[fallthrough]];
  case 3:                       // warning on fallthrough discouraged
    do {
      [[fallthrough]];          // error: next statement is not part of the same substatement execution
    } while (false);
  case 6:
    do {
      [[fallthrough]];          // error: next statement is not part of the same substatement execution
    } while (n--);
  case 7:
    while (false) {
      [[fallthrough]];          // error: next statement is not part of the same substatement execution
    }
  case 5:
    h();
  case 4:                       // implementation may warn on fallthrough
    i();
    [[fallthrough]];            // error
  }
}
```

— *end example*]
