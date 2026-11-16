# 20.15.1 Requirements [meta.rqmts]

[Contents](../README.md) · up: [20.15 Metaprogramming and type traits \[meta\]](meta.md) · prev: [20.15 Metaprogramming and type traits \[meta\]](meta.md) · next: [20.15.2 Header `<type_traits>` synopsis \[meta.type.synop\]](meta.type.synop.md)

**1** A *Cpp17UnaryTypeTrait* describes a property of a type. It shall be a class template that takes one template type argument and, optionally, additional arguments that help define the property being described. It shall be *Cpp17DefaultConstructible*, *Cpp17CopyConstructible*, and publicly and unambiguously derived, directly or indirectly, from its *base characteristic*, which is a specialization of the template `integral_constant` ([[meta.help]](meta.help.md)), with the arguments to the template `integral_constant` determined by the requirements for the particular property being described. The member names of the base characteristic shall not be hidden and shall be unambiguously available in the *Cpp17UnaryTypeTrait*.

**2** A *Cpp17BinaryTypeTrait* describes a relationship between two types. It shall be a class template that takes two template type arguments and, optionally, additional arguments that help define the relationship being described. It shall be *Cpp17DefaultConstructible*, *Cpp17CopyConstructible*, and publicly and unambiguously derived, directly or indirectly, from its *base characteristic*, which is a specialization of the template `integral_constant` ([[meta.help]](meta.help.md)), with the arguments to the template `integral_constant` determined by the requirements for the particular relationship being described. The member names of the base characteristic shall not be hidden and shall be unambiguously available in the *Cpp17BinaryTypeTrait*.

**3** A *Cpp17TransformationTrait* modifies a property of a type. It shall be a class template that takes one template type argument and, optionally, additional arguments that help define the modification. It shall define a publicly accessible nested type named `type`, which shall be a synonym for the modified type.

**4** Unless otherwise specified, the behavior of a program that adds specializations for any of the templates specified in this subclause [[meta]](meta.md) is undefined.

**5** Unless otherwise specified, an incomplete type may be used to instantiate a template specified in this subclause. The behavior of a program is undefined if:

- an instantiation of a template specified in subclause [[meta]](meta.md) directly or indirectly depends on an incompletely-defined object type `T`, and
- that instantiation could yield a different result were `T` hypothetically completed.
