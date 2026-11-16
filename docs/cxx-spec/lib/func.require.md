# 20.14.3 Requirements [func.require]

[Contents](../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.2 Definitions \[func.def\]](func.def.md) · next: [20.14.4 Function template `invoke` \[func.invoke\]](func.invoke.md)

**1** Define `INVOKE(f, t_1, t_2, ..., t_N)` as follows:

- `(t_1.*f)(t_2, ..., t_N)` when `f` is a pointer to a member function of a class `T` and `is_base_of_v<T, remove_reference_t<decltype(t_1)>>` is `true`;
- `(t_1.get().*f)(t_2, ..., t_N)` when `f` is a pointer to a member function of a class `T` and `remove_cvref_t<decltype(t_1)>` is a specialization of `reference_wrapper`;
- `((*t_1).*f)(t_2, ..., t_N)` when `f` is a pointer to a member function of a class `T` and `t_1` does not satisfy the previous two items;
- `t_1.*f` when `N == 1` and `f` is a pointer to data member of a class `T` and `is_base_of_v<T, remove_reference_t<decltype(t_1)>>` is `true`;
- `t_1.get().*f` when `N == 1` and `f` is a pointer to data member of a class `T` and `remove_cvref_t<decltype(t_1)>` is a specialization of `reference_wrapper`;
- `(*t_1).*f` when `N == 1` and `f` is a pointer to data member of a class `T` and `t_1` does not satisfy the previous two items;
- `f(t_1, t_2, ..., t_N)` in all other cases.

**2** Define `INVOKE<R>(f, t_1, t_2, ..., t_N)` as `static_cast<void>(INVOKE(f, t_1, t_2, ..., t_N))` if `R` is *cv* `void`, otherwise `INVOKE(f, t_1, t_2, ..., t_N)` implicitly converted to `R`.

**3** Every call wrapper ([[func.def]](func.def.md)) meets the *Cpp17MoveConstructible* and *Cpp17Destructible* requirements. An *argument forwarding call wrapper* is a call wrapper that can be called with an arbitrary argument list and delivers the arguments to the wrapped callable object as references. This forwarding step delivers rvalue arguments as rvalue references and lvalue arguments as lvalue references. [*Note 1*: In a typical implementation, argument forwarding call wrappers have an overloaded function call operator of the form

```cpp
template<class... UnBoundArgs>
  constexpr R operator()(UnBoundArgs&&... unbound_args) cv-qual;
```

— *end note*]

**4** A *perfect forwarding call wrapper* is an argument forwarding call wrapper that forwards its state entities to the underlying call expression. This forwarding step delivers a state entity of type `T` as *cv* `T&` when the call is performed on an lvalue of the call wrapper type and as *cv* `T&&` otherwise, where *cv* represents the cv-qualifiers of the call wrapper and where *cv* shall be neither `volatile` nor `const volatile`.

**5** A *call pattern* defines the semantics of invoking a perfect forwarding call wrapper. A postfix call performed on a perfect forwarding call wrapper is expression-equivalent ([[defns.expression-equivalent]](definitions.md#defns.expression-equivalent)) to an expression `e` determined from its call pattern `cp` by replacing all occurrences of the arguments of the call wrapper and its state entities with references as described in the corresponding forwarding steps.

**6** A *simple call wrapper* is a perfect forwarding call wrapper that meets the *Cpp17CopyConstructible* and *Cpp17CopyAssignable* requirements and whose copy constructor, move constructor, and assignment operators are constexpr functions that do not throw exceptions.

**7** The copy/move constructor of an argument forwarding call wrapper has the same apparent semantics as if memberwise copy/move of its state entities were performed ([[class.copy.ctor]](../class.copy.ctor.md)). [*Note 2*: This implies that each of the copy/move constructors has the same exception-specification as the corresponding implicit definition and is declared as `constexpr` if the corresponding implicit definition would be considered to be constexpr. — *end note*]

**8** Argument forwarding call wrappers returned by a given standard library function template have the same type if the types of their corresponding state entities are the same.
