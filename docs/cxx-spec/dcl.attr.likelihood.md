# 9.12.6 Likelihood attributes [dcl.attr.likelihood]

[Contents](README.md) · up: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · prev: [9.12.5 Fallthrough attribute \[dcl.attr.fallthrough\]](dcl.attr.fallthrough.md) · next: [9.12.7 Maybe unused attribute \[dcl.attr.unused\]](dcl.attr.unused.md)

**1** The *attribute-token*s `likely` and `unlikely` may be applied to labels or statements. The *attribute-token*s `likely` and `unlikely` shall appear at most once in each *attribute-list* and no *attribute-argument-clause* shall be present. The *attribute-token* `likely` shall not appear in an *attribute-specifier-seq* that contains the *attribute-token* `unlikely`.

**2** *Recommended practice:* The use of the `likely` attribute is intended to allow implementations to optimize for the case where paths of execution including it are arbitrarily more likely than any alternative path of execution that does not include such an attribute on a statement or label. The use of the `unlikely` attribute is intended to allow implementations to optimize for the case where paths of execution including it are arbitrarily more unlikely than any alternative path of execution that does not include such an attribute on a statement or label. A path of execution includes a label if and only if it contains a jump to that label. [*Note 1*: Excessive usage of either of these attributes is liable to result in performance degradation. — *end note*]

**3** [*Example 1*:

```cpp
void g(int);
int f(int n) {
  if (n > 5) [[unlikely]] {     // n > 5 is considered to be arbitrarily unlikely
    g(0);
    return n * 2 + 1;
  }

  switch (n) {
  case 1:
    g(1);
    [[fallthrough]];

  [[likely]] case 2:            // n == 2 is considered to be arbitrarily more
    g(2);                       // likely than any other value of n
    break;
  }
  return 3;
}
```

— *end example*]
