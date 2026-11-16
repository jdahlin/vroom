# 20.15.6 Relationships between types [meta.rel]

[Contents](../README.md) · up: [20.15 Metaprogramming and type traits \[meta\]](meta.md) · prev: [20.15.5 Type property queries \[meta.unary.prop.query\]](meta.unary.prop.query.md) · next: [20.15.7 Transformations between types \[meta.trans\]](meta.trans.md)

**1** This subclause contains templates that may be used to query relationships between types at compile time.

**2** Each of these templates shall be a *Cpp17BinaryTypeTrait* ([[meta.rqmts]](meta.rqmts.md)) with a base characteristic of `true_type` if the corresponding condition is true, otherwise `false_type`.

<a id="tab:meta.rel"></a>
**Table 51: Type relationship predicates** [tab:meta.rel]

| Template | Condition | Comments |
| --- | --- | --- |
| `template<class T, class U>`<br>`struct is_same;` | `T` and `U` name the same type with the same cv-qualifications |  |
| `template<class Base, class Derived>`<br>`struct is_base_of;` | `Base` is a base class of `Derived` ([[class.derived]](../class.derived.md)) without regard to cv-qualifiers or `Base` and `Derived` are not unions and name the same class type without regard to cv-qualifiers | If `Base` and `Derived` are non-union class types and are not possibly cv-qualified versions of the same type, `Derived` shall be a complete type. [*Note 1*: Base classes that are private, protected, or ambiguous are, nonetheless, base classes. — *end note*] |
| `template<class From, class To>`<br>`struct is_convertible;` | *see below* | `From` and `To` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class From, class To>`<br>`struct is_nothrow_convertible;` | `is_convertible_v<From, To>` is `true` and the conversion, as defined by `is_convertible`, is known not to throw any exceptions ([[expr.unary.noexcept]](../expr.unary.noexcept.md)) | `From` and `To` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T, class U>`<br>`struct is_layout_compatible;` | `T` and `U` are layout-compatible ([[basic.types]](../basic.types.md)) | `T` and `U` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class Base, class Derived>`<br>`struct is_pointer_interconvertible_base_of;` | `Derived` is unambiguously derived from `Base` without regard to cv-qualifiers, and each object of type `Derived` is pointer-interconvertible ([[basic.compound]](../basic.compound.md)) with its `Base` subobject, or `Base` and `Derived` are not unions and name the same class type without regard to cv-qualifiers. | If `Base` and `Derived` are non-union class types and are not (possibly cv-qualified versions of) the same type, `Derived` shall be a complete type. |
| `template<class Fn, class... ArgTypes>`<br>`struct is_invocable;` | The expression `INVOKE(declval<Fn>(), declval<ArgTypes>()...)` is well-formed when treated as an unevaluated operand | `Fn` and all types in the template parameter pack `ArgTypes` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class R, class Fn, class... ArgTypes>`<br>`struct is_invocable_r;` | The expression `INVOKE<R>(declval<Fn>(), declval<ArgTypes>()...)` is well-formed when treated as an unevaluated operand | `Fn`, `R`, and all types in the template parameter pack `ArgTypes` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class Fn, class... ArgTypes>`<br>`struct is_nothrow_invocable;` | `is_invocable_v<`<br>`Fn, ArgTypes...>` is `true` and the expression `INVOKE(declval<Fn>(), declval<ArgTypes>()...)` is known not to throw any exceptions ([[expr.unary.noexcept]](../expr.unary.noexcept.md)) | `Fn` and all types in the template parameter pack `ArgTypes` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class R, class Fn, class... ArgTypes>`<br>`struct is_nothrow_invocable_r;` | `is_invocable_r_v<`<br>`R, Fn, ArgTypes...>` is `true` and the expression `INVOKE<R>(declval<Fn>(), declval<ArgTypes>()...)` is known not to throw any exceptions ([[expr.unary.noexcept]](../expr.unary.noexcept.md)) | `Fn`, `R`, and all types in the template parameter pack `ArgTypes` shall be complete types, *cv* `void`, or arrays of unknown bound. |

**3** For the purpose of defining the templates in this subclause, a function call expression `declval<T>()` for any type `T` is considered to be a trivial ([[basic.types]](../basic.types.md), [[special]](../special.md)) function call that is not an odr-use ([[basic.def.odr]](../basic.def.odr.md)) of `declval` in the context of the corresponding definition notwithstanding the restrictions of [[declval]](declval.md).

**4** [*Example 1*:

```cpp
struct B {};
struct B1 : B {};
struct B2 : B {};
struct D : private B1, private B2 {};

is_base_of_v<B, D>              // true
is_base_of_v<const B, D>        // true
is_base_of_v<B, const D>        // true
is_base_of_v<B, const B>        // true
is_base_of_v<D, B>              // false
is_base_of_v<B&, D&>            // false
is_base_of_v<B[3], D[3]>        // false
is_base_of_v<int, int>          // false
```

— *end example*]

**5** The predicate condition for a template specialization `is_convertible<From, To>` shall be satisfied if and only if the return expression in the following code would be well-formed, including any implicit conversions to the return type of the function:

```cpp
To test() {
  return declval<From>();
}
```

[*Note 2*: This requirement gives well-defined results for reference types, void types, array types, and function types. — *end note*] Access checking is performed in a context unrelated to `To` and `From`. Only the validity of the immediate context of the *expression* of the `return` statement ([[stmt.return]](../stmt.return.md)) (including initialization of the returned object or reference) is considered. [*Note 3*: The initialization can result in side effects such as the instantiation of class template specializations and function template specializations, the generation of implicitly-defined functions, and so on. Such side effects are not in the “immediate context” and can result in the program being ill-formed. — *end note*]
