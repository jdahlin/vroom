# 20.15.7.6 Other transformations [meta.trans.other]

[Contents](../../README.md) · up: [20.15.7 Transformations between types \[meta.trans\]](meta.trans.md) · prev: [20.15.7.5 Pointer modifications \[meta.trans.ptr\]](meta.trans.ptr.md) · next: [20.15.8 Logical operator traits \[meta.logical\]](meta.logical.md)

<a id="tab:meta.trans.other"></a>
**Table 57: Other transformations** [tab:meta.trans.other]

| Template | Comments |
| --- | --- |
| `template<class T> struct type_identity;` | The member typedef `type` names the type `T`. |
| `template<size_t Len, size_t Align = default-alignment> struct aligned_storage;` | The value of *default-alignment* shall be the most stringent alignment requirement for any C++ object type whose size is no greater than `Len` ([[basic.types]](../../basic/basic.types.md)). The member typedef `type` shall be a trivial standard-layout type suitable for use as uninitialized storage for any object whose size is at most `Len` and whose alignment is a divisor of `Align`.<br>*Mandates:* `Len` is not zero. `Align` is equal to `alignof(T)` for some type `T` or to *default-alignment*. |
| `template<size_t Len, class... Types> struct aligned_union;` | The member typedef `type` shall be a trivial standard-layout type suitable for use as uninitialized storage for any object whose type is listed in `Types`; its size shall be at least `Len`. The static member `alignment_value` shall be an integral constant of type `size_t` whose value is the strictest alignment of all types listed in `Types`.<br>*Mandates:* At least one type is provided. Each type in the template parameter pack `Types` is a complete object type. |
| `template<class T> struct remove_cvref;` | The member typedef `type` names the same type as `remove_cv_t<remove_reference_t<T>>`. |
| `template<class T> struct decay;` | Let `U` be `remove_reference_t<T>`. If `is_array_v<U>` is `true`, the member typedef `type` equals `remove_extent_t<U>*`. If `is_function_v<U>` is `true`, the member typedef `type` equals `add_pointer_t<U>`. Otherwise the member typedef `type` equals `remove_cv_t<U>`. [*Note 1*: This behavior is similar to the lvalue-to-rvalue ([[conv.lval]](../../expr/conv.lval.md)), array-to-pointer ([[conv.array]](../../expr/conv.array.md)), and function-to-pointer ([[conv.func]](../../expr/conv.func.md)) conversions applied when an lvalue is used as an rvalue, but also strips cv-qualifiers from class types in order to more closely model by-value argument passing. — *end note*] |
| `template<bool B, class T = void>` `struct enable_if;` | If `B` is `true`, the member typedef `type` shall equal `T`; otherwise, there shall be no member `type`. |
| `template<bool B, class T,` `class F>`<br>`struct conditional;` | If `B` is `true`, the member typedef `type` shall equal `T`. If `B` is `false`, the member typedef `type` shall equal `F`. |
| `template<class... T>` `struct common_type;` | Unless this trait is specialized (as specified in Note B, below), the member `type` is defined or omitted as specified in Note A, below. If it is omitted, there shall be no member `type`. Each type in the template parameter pack `T` shall be complete, *cv* `void`, or an array of unknown bound. |
| `template<class, class,` `template<class> class,` `template<class> class>` `struct` `basic_common_reference;` | Unless this trait is specialized (as specified in Note D, below), there shall be no member `type`. |
| `template<class... T>` `struct common_reference;` | The member *typedef-name* `type` is defined or omitted as specified in Note C, below. Each type in the parameter pack `T` shall be complete or *cv* `void`. |
| `template<class T>`<br>`struct underlying_type;` | If `T` is an enumeration type, the member typedef `type` names the underlying type of `T` ([[dcl.enum]](../../dcl/dcl.enum.md)); otherwise, there is no member `type`.<br>*Mandates:* `T` is not an incomplete enumeration type. |
| `template<class Fn,`<br>`class... ArgTypes>`<br>`struct invoke_result;` | If the expression `INVOKE(declval<Fn>(), declval<ArgTypes>()...)` is well-formed when treated as an unevaluated operand ([[expr.prop]](../../expr/expr.prop.md)), the member typedef `type` names the type `decltype(INVOKE(declval<Fn>(), declval<ArgTypes>()...))`; otherwise, there shall be no member `type`. Access checking is performed as if in a context unrelated to `Fn` and `ArgTypes`. Only the validity of the immediate context of the expression is considered. [*Note 2*: The compilation of the expression can result in side effects such as the instantiation of class template specializations and function template specializations, the generation of implicitly-defined functions, and so on. Such side effects are not in the “immediate context” and can result in the program being ill-formed. — *end note*] <br>*Preconditions:* `Fn` and all types in the template parameter pack `ArgTypes` are complete types, *cv* `void`, or arrays of unknown bound. |
| `template<class T>` `struct unwrap_reference;` | If `T` is a specialization `reference_wrapper<X>` for some type `X`, the member typedef `type` of `unwrap_reference<T>` is `X&`, otherwise it is `T`. |
| `template<class T>` `unwrap_ref_decay;` | The member typedef `type` of `unwrap_ref_decay<T>` denotes the type `unwrap_reference_t<decay_t<T>>`. |

**1** [*Note 3*: A typical implementation would define `aligned_storage` as:

```cpp
template<size_t Len, size_t Alignment>
struct aligned_storage {
  typedef struct {
    alignas(Alignment) unsigned char __data[Len];
  } type;
};
```

— *end note*]

**2** In addition to being available via inclusion of the `<type_traits>` header, the templates `unwrap_reference`, `unwrap_ref_decay`, `unwrap_reference_t`, and `unwrap_ref_decay_t` are available when the header `<functional>` ([[functional.syn]](functional.syn.md)) is included.

**3** Let:

- `CREF(A)` be `add_lvalue_reference_t<const remove_reference_t<A>>`,
- `XREF(A)` denote a unary alias template `T` such that `T<U>` denotes the same type as `U` with the addition of `A`’s cv and reference qualifiers, for a non-reference cv-unqualified type `U`,
- `COPYCV(FROM, TO)` be an alias for type `TO` with the addition of `FROM`’s top-level cv-qualifiers, [*Example 1*: `COPYCV(const int, volatile short)` is an alias for `const volatile short`. — *end example*]
- `COND-RES(X, Y)` be `decltype(false ? declval<X(&)()>()(): declval<Y(&)()>()())`.

Given types `A` and `B`, let `X` be `remove_reference_t<A>`, let `Y` be `remove_reference_t<B>`, and let `COMMON-REF(A, B)` be:

- If `A` and `B` are both lvalue reference types, `COMMON-REF(A, B)` is `COND-RES(COPYCV(X, Y) &, COPYCV(Y, X) &)` if that type exists and is a reference type.
- Otherwise, let `C` be `remove_reference_t<COMMON-REF(X&, Y&)>&&`. If `A` and `B` are both rvalue reference types, `C` is well-formed, and `is_convertible_v<A, C> && is_convertible_v<B, C>` is `true`, then `COMMON-REF(A, B)` is `C`.
- Otherwise, let `D` be `COMMON-REF(const X&, Y&)`. If `A` is an rvalue reference and `B` is an lvalue reference and `D` is well-formed and `is_convertible_v<A, D>` is `true`, then `COMMON-REF(A, B)` is `D`.
- Otherwise, if `A` is an lvalue reference and `B` is an rvalue reference, then `COMMON-REF(A, B)` is `COMMON-REF(B, A)`.
- Otherwise, `COMMON-REF(A, B)` is ill-formed.

If any of the types computed above is ill-formed, then `COMMON-REF(A, B)` is ill-formed.

**4** Note A: For the `common_type` trait applied to a template parameter pack `T` of types, the member `type` shall be either defined or not present as follows:

- If `sizeof...(T)` is zero, there shall be no member `type`.

- If `sizeof...(T)` is one, let `T0` denote the sole type constituting the pack `T`. The member *typedef-name* `type` shall denote the same type, if any, as `common_type_t<T0, T0>`; otherwise there shall be no member `type`.

- If `sizeof...(T)` is two, let the first and second types constituting `T` be denoted by `T1` and `T2`, respectively, and let `D1` and `D2` denote the same types as `decay_t<T1>` and `decay_t<T2>`, respectively.

  - If `is_same_v<T1, D1>` is `false` or `is_same_v<T2, D2>` is `false`, let `C` denote the same type, if any, as `common_type_t<D1, D2>`.

  - [*Note 4*: None of the following will apply if there is a specialization `common_type<D1, D2>`. — *end note*]

  - Otherwise, if

    ```cpp
    decay_t<decltype(false ? declval<D1>() : declval<D2>())>
    ```

    denotes a valid type, let `C` denote that type.

  - Otherwise, if `COND-RES(CREF(D1), CREF(D2))` denotes a type, let `C` denote the type `decay_t<COND-RES(CREF(D1), CREF(D2))>`.

  In either case, the member *typedef-name* `type` shall denote the same type, if any, as `C`. Otherwise, there shall be no member `type`.

- If `sizeof...(T)` is greater than two, let `T1`, `T2`, and `R`, respectively, denote the first, second, and (pack of) remaining types constituting `T`. Let `C` denote the same type, if any, as `common_type_t<T1, T2>`. If there is such a type `C`, the member *typedef-name* `type` shall denote the same type, if any, as `common_type_t<C, R...>`. Otherwise, there shall be no member `type`.

**5** Note B: Notwithstanding the provisions of [[meta.type.synop]](meta.type.synop.md), and pursuant to [[namespace.std]](../library/namespace.std.md), a program may specialize `common_type<T1, T2>` for types `T1` and `T2` such that `is_same_v<T1, decay_t<T1>>` and `is_same_v<T2, decay_t<T2>>` are each `true`. [*Note 5*: Such specializations are needed when only explicit conversions are desired between the template arguments. — *end note*] Such a specialization need not have a member named `type`, but if it does, that member shall be a *typedef-name* for an accessible and unambiguous cv-unqualified non-reference type `C` to which each of the types `T1` and `T2` is explicitly convertible. Moreover, `common_type_t<T1, T2>` shall denote the same type, if any, as does `common_type_t<T2, T1>`. No diagnostic is required for a violation of this Note’s rules.

**6** Note C: For the `common_reference` trait applied to a parameter pack `T` of types, the member `type` shall be either defined or not present as follows:

- If `sizeof...(T)` is zero, there shall be no member `type`.

- Otherwise, if `sizeof...(T)` is one, let `T0` denote the sole type in the pack `T`. The member typedef `type` shall denote the same type as `T0`.

- Otherwise, if `sizeof...(T)` is two, let `T1` and `T2` denote the two types in the pack `T`. Then

  - If `T1` and `T2` are reference types and `COMMON-REF(T1, T2)` is well-formed, then the member typedef `type` denotes that type.
  - Otherwise, if `basic_common_reference<remove_cvref_t<T1>, remove_cvref_t<T2>, XREF(T1), XREF(T2)>::type` is well-formed, then the member typedef `type` denotes that type.
  - Otherwise, if `COND-RES(T1, T2)` is well-formed, then the member typedef `type` denotes that type.
  - Otherwise, if `common_type_t<T1, T2>` is well-formed, then the member typedef `type` denotes that type.
  - Otherwise, there shall be no member `type`.

- Otherwise, if `sizeof...(T)` is greater than two, let `T1`, `T2`, and `Rest`, respectively, denote the first, second, and (pack of) remaining types comprising `T`. Let `C` be the type `common_reference_t<T1, T2>`. Then:

  - If there is such a type `C`, the member typedef `type` shall denote the same type, if any, as `common_reference_t<C, Rest...>`.
  - Otherwise, there shall be no member `type`.

**7** Note D: Notwithstanding the provisions of [[meta.type.synop]](meta.type.synop.md), and pursuant to [[namespace.std]](../library/namespace.std.md), a program may partially specialize `basic_common_reference<T, U, TQual, UQual>` for types `T` and `U` such that `is_same_v<T, decay_t<T>>` and `is_same_v<U, decay_t<U>>` are each `true`. [*Note 6*: Such specializations can be used to influence the result of `common_reference`, and are needed when only explicit conversions are desired between the template arguments. — *end note*] Such a specialization need not have a member named `type`, but if it does, that member shall be a *typedef-name* for an accessible and unambiguous type `C` to which each of the types `TQual<T>` and `UQual<U>` is convertible. Moreover, `basic_common_reference<T, U, TQual, UQual>::type` shall denote the same type, if any, as does `basic_common_reference<U, T, UQual, TQual>::type`. No diagnostic is required for a violation of these rules.

**8** [*Example 2*: Given these definitions:

```cpp
using PF1 = bool  (&)();
using PF2 = short (*)(long);

struct S {
  operator PF2() const;
  double operator()(char, int&);
  void fn(long) const;
  char data;
};

using PMF = void (S::*)(long) const;
using PMD = char  S::*;
```

the following assertions will hold:

```cpp
static_assert(is_same_v<invoke_result_t<S, int>, short>);
static_assert(is_same_v<invoke_result_t<S&, unsigned char, int&>, double>);
static_assert(is_same_v<invoke_result_t<PF1>, bool>);
static_assert(is_same_v<invoke_result_t<PMF, unique_ptr<S>, int>, void>);
static_assert(is_same_v<invoke_result_t<PMD, S>, char&&>);
static_assert(is_same_v<invoke_result_t<PMD, const S*>, const char&>);
```

— *end example*]
