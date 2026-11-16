# 20.15.8 Logical operator traits [meta.logical]

[Contents](../../README.md) · up: [20.15 Metaprogramming and type traits \[meta\]](meta.md) · prev: [20.15.7.6 Other transformations \[meta.trans.other\]](meta.trans.other.md) · next: [20.15.9 Member relationships \[meta.member\]](meta.member.md)

**1** This subclause describes type traits for applying logical operators to other type traits.

```cpp
template<class... B> struct conjunction : see below { };
```

**2** The class template `conjunction` forms the logical conjunction of its template type arguments.

**3** For a specialization `conjunction<B_1, ..., B_N>`, if there is a template type argument `B`<sub>*i*</sub> for which `bool(B_i::value)` is `false`, then instantiating `conjunction<B_1, ..., B_N>::value` does not require the instantiation of `B_j::value` for *j* > *i*. [*Note 1*: This is analogous to the short-circuiting behavior of the built-in operator `&&`. — *end note*]

**4** Every template type argument for which `B_i::value` is instantiated shall be usable as a base class and shall have a member `value` which is convertible to `bool`, is not hidden, and is unambiguously available in the type.

**5** The specialization `conjunction<B_1, ..., B_N>` has a public and unambiguous base that is either

- the first type `B`<sub>*i*</sub> in the list `true_type, B_1, ..., B_N` for which `bool(B_i::value)` is `false`, or
- if there is no such `B`<sub>*i*</sub>, the last type in the list.

[*Note 2*: This means a specialization of `conjunction` does not necessarily inherit from either `true_type` or `false_type`. — *end note*]

**6** The member names of the base class, other than `conjunction` and `operator=`, shall not be hidden and shall be unambiguously available in `conjunction`.

```cpp
template<class... B> struct disjunction : see below { };
```

**7** The class template `disjunction` forms the logical disjunction of its template type arguments.

**8** For a specialization `disjunction<B_1, ..., B_N>`, if there is a template type argument `B`<sub>*i*</sub> for which `bool(B_i::value)` is `true`, then instantiating `disjunction<B_1, ..., B_N>::value` does not require the instantiation of `B_j::value` for *j* > *i*. [*Note 3*: This is analogous to the short-circuiting behavior of the built-in operator `||`. — *end note*]

**9** Every template type argument for which `B_i::value` is instantiated shall be usable as a base class and shall have a member `value` which is convertible to `bool`, is not hidden, and is unambiguously available in the type.

**10** The specialization `disjunction<B_1, ..., B_N>` has a public and unambiguous base that is either

- the first type `B`<sub>*i*</sub> in the list `false_type, B_1, ..., B_N` for which `bool(B_i::value)` is `true`, or
- if there is no such `B`<sub>*i*</sub>, the last type in the list.

[*Note 4*: This means a specialization of `disjunction` does not necessarily inherit from either `true_type` or `false_type`. — *end note*]

**11** The member names of the base class, other than `disjunction` and `operator=`, shall not be hidden and shall be unambiguously available in `disjunction`.

```cpp
template<class B> struct negation : see below { };
```

**12** The class template `negation` forms the logical negation of its template type argument. The type `negation<B>` is a *Cpp17UnaryTypeTrait* with a base characteristic of `bool_constant<!bool(B::value)>`.
