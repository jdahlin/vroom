# 16.5.3.1 Template argument requirements [utility.arg.requirements]

[Contents](../README.md) · up: [16.5.3 Requirements on types and expressions \[utility.requirements\]](utility.requirements.md) · prev: [16.5.3 Requirements on types and expressions \[utility.requirements\]](utility.requirements.md) · next: [16.5.3.2 Swappable requirements \[swappable.requirements\]](swappable.requirements.md)

**1** The template definitions in the C++ standard library refer to various named requirements whose details are set out in Tables [[tab:cpp17.equalitycomparable]](utility.arg.requirements.md#tab:cpp17.equalitycomparable)–[[tab:cpp17.destructible]](utility.arg.requirements.md#tab:cpp17.destructible). In these tables, `T` is an object or reference type to be supplied by a C++ program instantiating a template; `a`, `b`, and `c` are values of type (possibly `const`) `T`; `s` and `t` are modifiable lvalues of type `T`; `u` denotes an identifier; `rv` is an rvalue of type `T`; and `v` is an lvalue of type (possibly `const`) `T` or an rvalue of type `const T`.

**2** In general, a default constructor is not required. Certain container class member function signatures specify `T()` as a default argument. `T()` shall be a well-defined expression ([[dcl.init]](../dcl.init.md)) if one of those signatures is called using the default argument ([[dcl.fct.default]](../dcl.fct.default.md)).

<a id="tab:cpp17.equalitycomparable"></a>
**Table 25: *Cpp17EqualityComparable* requirements** [tab:cpp17.equalitycomparable]

| Expression | Return type | Requirement |
| --- | --- | --- |
| `a == b` | convertible to `bool` | `==` is an equivalence relation, that is, it has the following properties:<br>- For all `a`, `a == a`.<br>- If `a == b`, then `b == a`.<br>- If `a == b` and `b == c`, then `a == c`. |

<a id="tab:cpp17.lessthancomparable"></a>
**Table 26: *Cpp17LessThanComparable* requirements** [tab:cpp17.lessthancomparable]

| Expression | Return type | Requirement |
| --- | --- | --- |
| `a < b` | convertible to `bool` | `<` is a strict weak ordering relation ([[alg.sorting]](alg.sorting.md)) |

<a id="tab:cpp17.defaultconstructible"></a>
**Table 27: *Cpp17DefaultConstructible* requirements** [tab:cpp17.defaultconstructible]

| Expression | Post-condition |
| --- | --- |
| `T t;` | object `t` is default-initialized |
| `T u{};` | object `u` is value-initialized or aggregate-initialized |
| `T()`<br>`T{}` | an object of type `T` is value-initialized or aggregate-initialized |

<a id="tab:cpp17.moveconstructible"></a>
**Table 28: *Cpp17MoveConstructible* requirements** [tab:cpp17.moveconstructible]

| Expression | Post-condition |
| --- | --- |
| `T u = rv;` | `u` is equivalent to the value of `rv` before the construction |
| `T(rv)` | `T(rv)` is equivalent to the value of `rv` before the construction |
| `rv`’s state is unspecified [*Note 1*: `rv` must still meet the requirements of the library component that is using it. The operations listed in those requirements must work as specified whether `rv` has been moved from or not. — *end note*] |  |

<a id="tab:cpp17.copyconstructible"></a>
**Table 29: *Cpp17CopyConstructible* requirements (in addition to *Cpp17MoveConstructible*)** [tab:cpp17.copyconstructible]

| Expression | Post-condition |
| --- | --- |
| `T u = v;` | the value of `v` is unchanged and is equivalent to `u` |
| `T(v)` | the value of `v` is unchanged and is equivalent to `T(v)` |

<a id="tab:cpp17.moveassignable"></a>
**Table 30: *Cpp17MoveAssignable* requirements** [tab:cpp17.moveassignable]

| Expression | Return type | Return value | Post-condition |
| --- | --- | --- | --- |
| `t = rv` | `T&` | `t` | If `t` and `rv` do not refer to the same object, `t` is equivalent to the value of `rv` before the assignment |
| `rv`’s state is unspecified. [*Note 2*: `rv` must still meet the requirements of the library component that is using it, whether or not `t` and `rv` refer to the same object. The operations listed in those requirements must work as specified whether `rv` has been moved from or not. — *end note*] |  |  |  |

<a id="tab:cpp17.copyassignable"></a>
**Table 31: *Cpp17CopyAssignable* requirements (in addition to *Cpp17MoveAssignable*)** [tab:cpp17.copyassignable]

| Expression | Return type | Return value | Post-condition |
| --- | --- | --- | --- |
| `t = v` | `T&` | `t` | `t` is equivalent to `v`, the value of `v` is unchanged |

<a id="tab:cpp17.destructible"></a>
**Table 32: *Cpp17Destructible* requirements** [tab:cpp17.destructible]

| Expression | Post-condition |
| --- | --- |
| `u.~T()` | All resources owned by `u` are reclaimed, no exception is propagated. |
| [*Note 3*: Array types and non-object types are not *Cpp17Destructible*. — *end note*] |  |
