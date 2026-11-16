# 20.15.9 Member relationships [meta.member]

[Contents](../../README.md) · up: [20.15 Metaprogramming and type traits \[meta\]](meta.md) · prev: [20.15.8 Logical operator traits \[meta.logical\]](meta.logical.md) · next: [20.15.10 Constant evaluation context \[meta.const.eval\]](meta.const.eval.md)

```cpp
template<class S, class M>
  constexpr bool is_pointer_interconvertible_with_class(M S::*m) noexcept;
```

**1** *Mandates:* `S` is a complete type.

**2** *Returns:* `true` if and only if `S` is a standard-layout type, `M` is an object type, `m` is not null, and each object `s` of type `S` is pointer-interconvertible ([[basic.compound]](../../basic/basic.compound.md)) with its subobject `s.*m`.

```cpp
template<class S1, class S2, class M1, class M2>
  constexpr bool is_corresponding_member(M1 S1::*m1, M2 S2::*m2) noexcept;
```

**3** *Mandates:* `S1` and `S2` are complete types.

**4** *Returns:* `true` if and only if `S1` and `S2` are standard-layout types, `M1` and `M2` are object types, `m1` and `m2` are not null, and `m1` and `m2` point to corresponding members of the common initial sequence ([[class.mem]](../../class/class.mem.md)) of `S1` and `S2`.

**5** [*Note 1*: The type of a pointer-to-member expression `&C::b` is not always a pointer to member of `C`, leading to potentially surprising results when using these functions in conjunction with inheritance. [*Example 1*:

```cpp
struct A { int a; };                    // a standard-layout class
struct B { int b; };                    // a standard-layout class
struct C: public A, public B { };       // not a standard-layout class

static_assert( is_pointer_interconvertible_with_class( &C::b ) );
  // Succeeds because, despite its appearance, &C::b has type
  // ``pointer to member of B of type int''.
static_assert( is_pointer_interconvertible_with_class<C>( &C::b ) );
  // Forces the use of class C, and fails.

static_assert( is_corresponding_member( &C::a, &C::b ) );
  // Succeeds because, despite its appearance, &C::a and &C::b have types
  // ``pointer to member of A of type int'' and
  // ``pointer to member of B of type int'', respectively.
static_assert( is_corresponding_member<C, C>( &C::a, &C::b ) );
  // Forces the use of class C, and fails.
```

— *end example*] — *end note*]
