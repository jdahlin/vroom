# 9.12.8 Nodiscard attribute [dcl.attr.nodiscard]

[Contents](../README.md) · up: [9.12 Attributes \[dcl.attr\]](dcl.attr.md) · prev: [9.12.7 Maybe unused attribute \[dcl.attr.unused\]](dcl.attr.unused.md) · next: [9.12.9 Noreturn attribute \[dcl.attr.noreturn\]](dcl.attr.noreturn.md)

**1** The *attribute-token* `nodiscard` may be applied to the *declarator-id* in a function declaration or to the declaration of a class or enumeration. It shall appear at most once in each *attribute-list*. An *attribute-argument-clause* may be present and, if present, shall have the form:

```bnf
'(' string-literal ')'
```

**2** A name or entity declared without the `nodiscard` attribute can later be redeclared with the attribute and vice-versa. [*Note 1*: Thus, an entity initially declared without the attribute can be marked as `nodiscard` by a subsequent redeclaration. However, after an entity is marked as `nodiscard`, later redeclarations do not remove the `nodiscard` from the entity. — *end note*] Redeclarations using different forms of the attribute (with or without the *attribute-argument-clause* or with different *attribute-argument-clause*s) are allowed.

**3** A *nodiscard type* is a (possibly cv-qualified) class or enumeration type marked `nodiscard` in a reachable declaration. A *nodiscard call* is either

- a function call expression ([[expr.call]](../expr/expr.call.md)) that calls a function declared `nodiscard` in a reachable declaration or whose return type is a nodiscard type, or
- an explicit type conversion ([[expr.type.conv]](../expr/expr.type.conv.md), [[expr.static.cast]](../expr/expr.static.cast.md), [[expr.cast]](../expr/expr.cast.md)) that constructs an object through a constructor declared `nodiscard` in a reachable declaration, or that initializes an object of a nodiscard type.

**4** *Recommended practice:* Appearance of a nodiscard call as a potentially-evaluated discarded-value expression ([[expr.prop]](../expr/expr.prop.md)) is discouraged unless explicitly cast to `void`. Implementations should issue a warning in such cases. [*Note 2*: This is typically because discarding the return value of a nodiscard call has surprising consequences. — *end note*] The *string-literal* in a `nodiscard` *attribute-argument-clause* should be used in the message of the warning as the rationale for why the result should not be discarded.

**5** [*Example 1*:

```cpp
struct [[nodiscard]] my_scopeguard { /* ... */ };
struct my_unique {
  my_unique() = default;                                // does not acquire resource
  [[nodiscard]] my_unique(int fd) { /* ... */ }         // acquires resource
  ~my_unique() noexcept { /* ... */ }                   // releases resource, if any
  /* ... */
};
struct [[nodiscard]] error_info { /* ... */ };
error_info enable_missile_safety_mode();
void launch_missiles();
void test_missiles() {
  my_scopeguard();              // warning encouraged
  (void)my_scopeguard(),        // warning not encouraged, cast to void
    launch_missiles();          // comma operator, statement continues
  my_unique(42);                // warning encouraged
  my_unique();                  // warning not encouraged
  enable_missile_safety_mode(); // warning encouraged
  launch_missiles();
}
error_info &foo();
void f() { foo(); }             // warning not encouraged: not a nodiscard call, because neither
                                // the (reference) return type nor the function is declared nodiscard
```

— *end example*]
