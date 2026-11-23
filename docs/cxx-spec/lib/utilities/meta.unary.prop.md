# 20.15.4.3 Type properties [meta.unary.prop]

[Contents](../../README.md) · up: [20.15.4 Unary type traits \[meta.unary\]](meta.unary.md) · prev: [20.15.4.2 Composite type traits \[meta.unary.comp\]](meta.unary.comp.md) · next: [20.15.5 Type property queries \[meta.unary.prop.query\]](meta.unary.prop.query.md)

**1** These templates provide access to some of the more important properties of types.

**2** It is unspecified whether the library defines any full or partial specializations of any of these templates.

**3** For all of the class templates `X` declared in this subclause, instantiating that template with a template-argument that is a class template specialization may result in the implicit instantiation of the template argument if and only if the semantics of `X` require that the argument is a complete type.

**4** For the purpose of defining the templates in this subclause, a function call expression `declval<T>()` for any type `T` is considered to be a trivial ([[basic.types]](../../basic/basic.types.md), [[special]](../../class/special.md)) function call that is not an odr-use ([[basic.def.odr]](../../basic/basic.def.odr.md)) of `declval` in the context of the corresponding definition notwithstanding the restrictions of [[declval]](declval.md).

<a id="tab:meta.unary.prop"></a>
**Table 49: Type property predicates** [tab:meta.unary.prop]

| Template | Condition | Preconditions |
| --- | --- | --- |
| `template<class T>`<br>`struct is_const;` | `T` is const-qualified ([[basic.type.qualifier]](../../basic/basic.type.qualifier.md)) |  |
| `template<class T>`<br>`struct is_volatile;` | `T` is volatile-qualified ([[basic.type.qualifier]](../../basic/basic.type.qualifier.md)) |  |
| `template<class T>`<br>`struct is_trivial;` | `T` is a trivial type ([[basic.types]](../../basic/basic.types.md)) | `remove_all_extents_t<T>` shall be a complete type or *cv* `void`. |
| `template<class T>`<br>`struct is_trivially_copyable;` | `T` is a trivially copyable type ([[basic.types]](../../basic/basic.types.md)) | `remove_all_extents_t<T>` shall be a complete type or *cv* `void`. |
| `template<class T>`<br>`struct is_standard_layout;` | `T` is a standard-layout type ([[basic.types]](../../basic/basic.types.md)) | `remove_all_extents_t<T>` shall be a complete type or *cv* `void`. |
| `template<class T>`<br>`struct is_empty;` | `T` is a class type, but not a union type, with no non-static data members other than subobjects of zero size, no virtual member functions, no virtual base classes, and no base class `B` for which `is_empty_v<B>` is `false`. | If `T` is a non-union class type, `T` shall be a complete type. |
| `template<class T>`<br>`struct is_polymorphic;` | `T` is a polymorphic class ([[class.virtual]](../../class/class.virtual.md)) | If `T` is a non-union class type, `T` shall be a complete type. |
| `template<class T>`<br>`struct is_abstract;` | `T` is an abstract class ([[class.abstract]](../../class/class.abstract.md)) | If `T` is a non-union class type, `T` shall be a complete type. |
| `template<class T>`<br>`struct is_final;` | `T` is a class type marked with the *class-virt-specifier* `final` ([[class.pre]](../../class/class.pre.md)). [*Note 1*: A union is a class type that can be marked with `final`. — *end note*] | If `T` is a class type, `T` shall be a complete type. |
| `template<class T>`<br>`struct is_aggregate;` | `T` is an aggregate type ([[dcl.init.aggr]](../../dcl/dcl.init.aggr.md)) | `remove_all_extents_t<T>` shall be a complete type or *cv* `void`. |
| `template<class T>`<br>`struct is_signed;` | If `is_arithmetic_v<T>` is `true`, the same result as `T(-1) < T(0)`; otherwise, `false` |  |
| `template<class T>`<br>`struct is_unsigned;` | If `is_arithmetic_v<T>` is `true`, the same result as `T(0) < T(-1)`; otherwise, `false` |  |
| `template<class T>`<br>`struct is_bounded_array;` | `T` is an array type of known bound ([[dcl.array]](../../dcl/dcl.array.md)) |  |
| `template<class T>`<br>`struct is_unbounded_array;` | `T` is an array type of unknown bound ([[dcl.array]](../../dcl/dcl.array.md)) |  |
| `template<class T, class... Args>`<br>`struct is_constructible;` | For a function type `T` or for a *cv* `void` type `T`, `is_constructible_v<T, Args...>` is `false`, otherwise *see below* | `T` and all types in the template parameter pack `Args` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T>`<br>`struct is_default_constructible;` | `is_constructible_v<T>` is `true`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_copy_constructible;` | For a referenceable type `T` ([[defns.referenceable]](../library/definitions.md#defns.referenceable)), the same result as `is_constructible_v<T, const T&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_move_constructible;` | For a referenceable type `T`, the same result as `is_constructible_v<T, T&&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T, class U>`<br>`struct is_assignable;` | The expression `declval<T>() =` `declval<U>()` is well-formed when treated as an unevaluated operand ([[expr.prop]](../../expr/expr.prop.md)). Access checking is performed as if in a context unrelated to `T` and `U`. Only the validity of the immediate context of the assignment expression is considered. [*Note 2*: The compilation of the expression can result in side effects such as the instantiation of class template specializations and function template specializations, the generation of implicitly-defined functions, and so on. Such side effects are not in the “immediate context” and can result in the program being ill-formed. — *end note*] | `T` and `U` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T>`<br>`struct is_copy_assignable;` | For a referenceable type `T`, the same result as `is_assignable_v<T&, const T&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_move_assignable;` | For a referenceable type `T`, the same result as `is_assignable_v<T&, T&&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T, class U>`<br>`struct is_swappable_with;` | The expressions `swap(declval<T>(), declval<U>())` and `swap(declval<U>(), declval<T>())` are each well-formed when treated as an unevaluated operand ([[expr.prop]](../../expr/expr.prop.md)) in an overload-resolution context for swappable values ([[swappable.requirements]](../library/swappable.requirements.md)). Access checking is performed as if in a context unrelated to `T` and `U`. Only the validity of the immediate context of the `swap` expressions is considered. [*Note 3*: The compilation of the expressions can result in side effects such as the instantiation of class template specializations and function template specializations, the generation of implicitly-defined functions, and so on. Such side effects are not in the “immediate context” and can result in the program being ill-formed. — *end note*] | `T` and `U` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T>`<br>`struct is_swappable;` | For a referenceable type `T`, the same result as `is_swappable_with_v<T&, T&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_destructible;` | Either `T` is a reference type, or `T` is a complete object type for which the expression `declval<U&>().~U()` is well-formed when treated as an unevaluated operand ([[expr.prop]](../../expr/expr.prop.md)), where `U` is `remove_all_extents_t<T>`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T, class... Args>`<br>`struct`<br>`is_trivially_constructible;` | `is_constructible_v<T,`<br>`Args...>` is `true` and the variable definition for `is_constructible`, as defined below, is known to call no operation that is not trivial ([[basic.types]](../../basic/basic.types.md), [[special]](../../class/special.md)). | `T` and all types in the template parameter pack `Args` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T>`<br>`struct is_trivially_default_constructible;` | `is_trivially_constructible_v<T>` is `true`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_trivially_copy_constructible;` | For a referenceable type `T`, the same result as `is_trivially_constructible_v<T, const T&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_trivially_move_constructible;` | For a referenceable type `T`, the same result as `is_trivially_constructible_v<T, T&&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T, class U>`<br>`struct is_trivially_assignable;` | `is_assignable_v<T, U>` is `true` and the assignment, as defined by `is_assignable`, is known to call no operation that is not trivial ([[basic.types]](../../basic/basic.types.md), [[special]](../../class/special.md)). | `T` and `U` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T>`<br>`struct is_trivially_copy_assignable;` | For a referenceable type `T`, the same result as `is_trivially_assignable_v<T&, const T&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_trivially_move_assignable;` | For a referenceable type `T`, the same result as `is_trivially_assignable_v<T&, T&&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_trivially_destructible;` | `is_destructible_v<T>` is `true` and `remove_all_extents_t<T>` is either a non-class type or a class type with a trivial destructor. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T, class... Args>`<br>`struct is_nothrow_constructible;` | `is_constructible_v<T,` `Args...>` is `true` and the variable definition for `is_constructible`, as defined below, is known not to throw any exceptions ([[expr.unary.noexcept]](../../expr/expr.unary.noexcept.md)). | `T` and all types in the template parameter pack `Args` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T>`<br>`struct is_nothrow_default_constructible;` | `is_nothrow_constructible_v<T>` is `true`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_nothrow_copy_constructible;` | For a referenceable type `T`, the same result as `is_nothrow_constructible_v<T, const T&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_nothrow_move_constructible;` | For a referenceable type `T`, the same result as `is_nothrow_constructible_v<T, T&&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T, class U>`<br>`struct is_nothrow_assignable;` | `is_assignable_v<T, U>` is `true` and the assignment is known not to throw any exceptions ([[expr.unary.noexcept]](../../expr/expr.unary.noexcept.md)). | `T` and `U` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T>`<br>`struct is_nothrow_copy_assignable;` | For a referenceable type `T`, the same result as `is_nothrow_assignable_v<T&, const T&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_nothrow_move_assignable;` | For a referenceable type `T`, the same result as `is_nothrow_assignable_v<T&, T&&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T, class U>`<br>`struct is_nothrow_swappable_with;` | `is_swappable_with_v<T, U>` is `true` and each `swap` expression of the definition of `is_swappable_with<T, U>` is known not to throw any exceptions ([[expr.unary.noexcept]](../../expr/expr.unary.noexcept.md)). | `T` and `U` shall be complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T>`<br>`struct is_nothrow_swappable;` | For a referenceable type `T`, the same result as `is_nothrow_swappable_with_v<T&, T&>`, otherwise `false`. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct is_nothrow_destructible;` | `is_destructible_v<T>` is `true` and the indicated destructor is known not to throw any exceptions ([[expr.unary.noexcept]](../../expr/expr.unary.noexcept.md)). | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |
| `template<class T>`<br>`struct has_virtual_destructor;` | `T` has a virtual destructor ([[class.dtor]](../../class/class.dtor.md)) | If `T` is a non-union class type, `T` shall be a complete type. |
| `template<class T>`<br>`struct has_unique_object_representations;` | For an array type `T`, the same result as `has_unique_object_representations_v<remove_all_extents_t<T>>`, otherwise *see below*. | `T` shall be a complete type, *cv* `void`, or an array of unknown bound. |

**5** [*Example 1*:

```cpp
is_const_v<const volatile int>      // true
is_const_v<const int*>              // false
is_const_v<const int&>              // false
is_const_v<int[3]>                  // false
is_const_v<const int[3]>            // true
```

— *end example*]

**6** [*Example 2*:

```cpp
remove_const_t<const volatile int>  // volatile int
remove_const_t<const int* const>    // const int*
remove_const_t<const int&>          // const int&
remove_const_t<const int[3]>        // int[3]
```

— *end example*]

**7** [*Example 3*:

```cpp
// Given:
struct P final { };
union U1 { };
union U2 final { };

// the following assertions hold:
static_assert(!is_final_v<int>);
static_assert(is_final_v<P>);
static_assert(!is_final_v<U1>);
static_assert(is_final_v<U2>);
```

— *end example*]

**8** The predicate condition for a template specialization `is_constructible<T, Args...>` shall be satisfied if and only if the following variable definition would be well-formed for some invented variable `t`:

```cpp
T t(declval<Args>()...);
```

[*Note 4*: These tokens are never interpreted as a function declaration. — *end note*] Access checking is performed as if in a context unrelated to `T` and any of the `Args`. Only the validity of the immediate context of the variable initialization is considered. [*Note 5*: The evaluation of the initialization can result in side effects such as the instantiation of class template specializations and function template specializations, the generation of implicitly-defined functions, and so on. Such side effects are not in the “immediate context” and can result in the program being ill-formed. — *end note*]

**9** The predicate condition for a template specialization `has_unique_object_representations<T>` shall be satisfied if and only if:

- `T` is trivially copyable, and
- any two objects of type `T` with the same value have the same object representation, where two objects of array or non-union class type are considered to have the same value if their respective sequences of direct subobjects have the same values, and two objects of union type are considered to have the same value if they have the same active member and the corresponding members have the same value.

The set of scalar types for which this condition holds is implementation-defined. [*Note 6*: If a type has padding bits, the condition does not hold; otherwise, the condition holds true for integral types. — *end note*]
