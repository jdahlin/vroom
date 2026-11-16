# 9.2.2 Function specifiers [dcl.fct.spec]

[Contents](README.md) · up: [9.2 Specifiers \[dcl.spec\]](dcl.spec.md) · prev: [9.2.1 Storage class specifiers \[dcl.stc\]](dcl.stc.md) · next: [9.2.3 The `typedef` specifier \[dcl.typedef\]](dcl.typedef.md)

**1** A *function-specifier* can be used only in a function declaration.

```bnf
function-specifier:
    'virtual'
    explicit-specifier
```

```bnf
explicit-specifier:
    'explicit' '(' constant-expression ')'
    'explicit'
```

**2** The `virtual` specifier shall be used only in the initial declaration of a non-static class member function; see [[class.virtual]](class.virtual.md).

**3** An *explicit-specifier* shall be used only in the declaration of a constructor or conversion function within its class definition; see [[class.conv.ctor]](class.conv.ctor.md) and [[class.conv.fct]](class.conv.fct.md).

**4** In an *explicit-specifier*, the *constant-expression*, if supplied, shall be a contextually converted constant expression of type `bool` ([[expr.const]](expr.const.md)). The *explicit-specifier* `explicit` without a *constant-expression* is equivalent to the *explicit-specifier* `explicit(true)`. If the constant expression evaluates to `true`, the function is explicit. Otherwise, the function is not explicit. A `(` token that follows `explicit` is parsed as part of the *explicit-specifier*.
