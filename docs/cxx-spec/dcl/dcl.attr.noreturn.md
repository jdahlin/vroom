# 9.12.9 Noreturn attribute [dcl.attr.noreturn]

[Contents](../README.md) · up: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · prev: [9.12.8 Nodiscard attribute \[dcl.attr.nodiscard\]](dcl.attr.nodiscard.md) · next: [9.12.10 No unique address attribute \[dcl.attr.nouniqueaddr\]](dcl.attr.nouniqueaddr.md)

**1** The *attribute-token* `noreturn` specifies that a function does not return. It shall appear at most once in each *attribute-list* and no *attribute-argument-clause* shall be present. The attribute may be applied to the *declarator-id* in a function declaration. The first declaration of a function shall specify the `noreturn` attribute if any declaration of that function specifies the `noreturn` attribute. If a function is declared with the `noreturn` attribute in one translation unit and the same function is declared without the `noreturn` attribute in another translation unit, the program is ill-formed, no diagnostic required.

**2** If a function `f` is called where `f` was previously declared with the `noreturn` attribute and `f` eventually returns, the behavior is undefined. [*Note 1*: The function may terminate by throwing an exception. — *end note*]

**3** *Recommended practice:* Implementations should issue a warning if a function marked `[[noreturn]]` might return.

**4** [*Example 1*:

```cpp
[[ noreturn ]] void f() {
  throw "error";                // OK
}

[[ noreturn ]] void q(int i) {  // behavior is undefined if called with an argument <= 0
  if (i > 0)
    throw "positive";
}
```

— *end example*]
