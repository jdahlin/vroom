# 20.14.18 Class template `hash` [unord.hash]

[Contents](../../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.17.3 Class template `boyer_moore_horspool_searcher` \[func.search.bmh\]](func.search.bmh.md) · next: [20.15 Metaprogramming and type traits \[meta\]](meta.md)

**1** The unordered associative containers defined in [[unord]](../containers/unord.md) use specializations of the class template `hash` ([[functional.syn]](functional.syn.md)) as the default hash function.

**2** Each specialization of `hash` is either enabled or disabled, as described below. [*Note 1*: Enabled specializations meet the *Cpp17Hash* requirements, and disabled specializations do not. — *end note*] Each header that declares the template `hash` provides enabled specializations of `hash` for `nullptr_t` and all cv-unqualified arithmetic, enumeration, and pointer types. For any type `Key` for which neither the library nor the user provides an explicit or partial specialization of the class template `hash`, `hash<Key>` is disabled.

**3** If the library provides an explicit or partial specialization of `hash<Key>`, that specialization is enabled except as noted otherwise, and its member functions are `noexcept` except as noted otherwise.

**4** If `H` is a disabled specialization of `hash`, these values are `false`: `is_default_constructible_v<H>`, `is_copy_constructible_v<H>`, `is_move_constructible_v<H>`, `is_copy_assignable_v<H>`, and `is_move_assignable_v<H>`. Disabled specializations of `hash` are not function object types ([[function.objects]](function.objects.md)). [*Note 2*: This means that the specialization of `hash` exists, but any attempts to use it as a *Cpp17Hash* will be ill-formed. — *end note*]

**5** An enabled specialization `hash<Key>` will:

- meet the *Cpp17Hash* requirements ([Table 34](../library/hash.requirements.md#tab:cpp17.hash)), with `Key` as the function call argument type, the *Cpp17DefaultConstructible* requirements ([Table 27](../library/utility.arg.requirements.md#tab:cpp17.defaultconstructible)), the *Cpp17CopyAssignable* requirements ([Table 31](../library/utility.arg.requirements.md#tab:cpp17.copyassignable)),
- be swappable ([[swappable.requirements]](../library/swappable.requirements.md)) for lvalues,
- meet the requirement that if `k1 == k2` is `true`, `h(k1) == h(k2)` is also `true`, where `h` is an object of type `hash<Key>` and `k1` and `k2` are objects of type `Key`;
- meet the requirement that the expression `h(k)`, where `h` is an object of type `hash<Key>` and `k` is an object of type `Key`, shall not throw an exception unless `hash<Key>` is a program-defined specialization that depends on at least one program-defined type.
