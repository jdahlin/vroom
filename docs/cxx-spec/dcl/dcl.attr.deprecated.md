# 9.12.4 Deprecated attribute [dcl.attr.deprecated]

[Contents](../README.md) · up: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · prev: [9.12.3 Carries dependency attribute \[dcl.attr.depend\]](dcl.attr.depend.md) · next: [9.12.5 Fallthrough attribute \[dcl.attr.fallthrough\]](dcl.attr.fallthrough.md)

**1** The *attribute-token* `deprecated` can be used to mark names and entities whose use is still allowed, but is discouraged for some reason. [*Note 1*: In particular, `deprecated` is appropriate for names and entities that are deemed obsolescent or unsafe. — *end note*] It shall appear at most once in each *attribute-list*. An *attribute-argument-clause* may be present and, if present, it shall have the form:

```bnf
'(' string-literal ')'
```

[*Note 2*: The *string-literal* in the *attribute-argument-clause* could be used to explain the rationale for deprecation and/or to suggest a replacing entity. — *end note*]

**2** The attribute may be applied to the declaration of a class, a *typedef-name*, a variable, a non-static data member, a function, a namespace, an enumeration, an enumerator, or a template specialization.

**3** A name or entity declared without the `deprecated` attribute can later be redeclared with the attribute and vice-versa. [*Note 3*: Thus, an entity initially declared without the attribute can be marked as deprecated by a subsequent redeclaration. However, after an entity is marked as deprecated, later redeclarations do not un-deprecate the entity. — *end note*] Redeclarations using different forms of the attribute (with or without the *attribute-argument-clause* or with different *attribute-argument-clause*s) are allowed.

**4** *Recommended practice:* Implementations should use the `deprecated` attribute to produce a diagnostic message in case the program refers to a name or entity other than to declare it, after a declaration that specifies the attribute. The diagnostic message should include the text provided within the *attribute-argument-clause* of any `deprecated` attribute applied to the name or entity.
