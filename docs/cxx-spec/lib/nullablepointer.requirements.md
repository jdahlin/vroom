# 16.5.3.3 *Cpp17NullablePointer* requirements [nullablepointer.requirements]

[Contents](../README.md) · up: [16.5.3 Requirements on types and expressions \[utility.requirements\]](utility.requirements.md) · prev: [16.5.3.2 Swappable requirements \[swappable.requirements\]](swappable.requirements.md) · next: [16.5.3.4 *Cpp17Hash* requirements \[hash.requirements\]](hash.requirements.md)

**1** A *Cpp17NullablePointer* type is a pointer-like type that supports null values. A type `P` meets the *Cpp17NullablePointer* requirements if:

- `P` meets the *Cpp17EqualityComparable*, *Cpp17DefaultConstructible*, *Cpp17CopyConstructible*, *Cpp17CopyAssignable*, and *Cpp17Destructible* requirements,
- lvalues of type `P` are swappable ([[swappable.requirements]](swappable.requirements.md)),
- the expressions shown in [Table 33](nullablepointer.requirements.md#tab:cpp17.nullablepointer) are valid and have the indicated semantics, and
- `P` meets all the other requirements of this subclause.

**2** A value-initialized object of type `P` produces the null value of the type. The null value shall be equivalent only to itself. A default-initialized object of type `P` may have an indeterminate value. [*Note 1*: Operations involving indeterminate values may cause undefined behavior. — *end note*]

**3** An object `p` of type `P` can be contextually converted to `bool` ([[conv]](../conv.md)). The effect shall be as if `p != nullptr` had been evaluated in place of `p`.

**4** No operation which is part of the *Cpp17NullablePointer* requirements shall exit via an exception.

**5** In [Table 33](nullablepointer.requirements.md#tab:cpp17.nullablepointer), `u` denotes an identifier, `t` denotes a non-`const` lvalue of type `P`, `a` and `b` denote values of type (possibly `const`) `P`, and `np` denotes a value of type (possibly `const`) `std::nullptr_t`.

<a id="tab:cpp17.nullablepointer"></a>
**Table 33: *Cpp17NullablePointer* requirements** [tab:cpp17.nullablepointer]

| Expression | Return type | Operational semantics |
| --- | --- | --- |
| `P u(np);` |  | *Postconditions:* `u == nullptr` |
| `P u = np;` |  |  |
| `P(np)` |  | *Postconditions:* `P(np) == nullptr` |
| `t = np` | `P&` | *Postconditions:* `t == nullptr` |
| `a != b` | contextually convertible to `bool` | `!(a == b)` |
| `a == np` | contextually convertible to `bool` | `a == P()` |
| `np == a` |  |  |
| `a != np` | contextually convertible to `bool` | `!(a == np)` |
| `np != a` |  |  |
