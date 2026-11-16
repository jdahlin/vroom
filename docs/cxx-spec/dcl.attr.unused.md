# 9.12.7 Maybe unused attribute [dcl.attr.unused]

[Contents](README.md) · up: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · prev: [9.12.6 Likelihood attributes \[dcl.attr.likelihood\]](dcl.attr.likelihood.md) · next: [9.12.8 Nodiscard attribute \[dcl.attr.nodiscard\]](dcl.attr.nodiscard.md)

**1** The *attribute-token* `maybe_unused` indicates that a name or entity is possibly intentionally unused. It shall appear at most once in each *attribute-list* and no *attribute-argument-clause* shall be present.

**2** The attribute may be applied to the declaration of a class, a *typedef-name*, a variable (including a structured binding declaration), a non-static data member, a function, an enumeration, or an enumerator.

**3** A name or entity declared without the `maybe_unused` attribute can later be redeclared with the attribute and vice versa. An entity is considered marked after the first declaration that marks it.

**4** *Recommended practice:* For an entity marked `maybe_unused`, implementations should not emit a warning that the entity or its structured bindings (if any) are used or unused. For a structured binding declaration not marked `maybe_unused`, implementations should not emit such a warning unless all of its structured bindings are unused.

**5** [*Example 1*:

```cpp
[[maybe_unused]] void f([[maybe_unused]] bool thing1,
                        [[maybe_unused]] bool thing2) {
  [[maybe_unused]] bool b = thing1 && thing2;
  assert(b);
}
```

Implementations should not warn that `b` is unused, whether or not `NDEBUG` is defined. — *end example*]
