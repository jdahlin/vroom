# 16.4.1.3 Requirements [structure.requirements]

[Contents](../../README.md) · up: [16.4.1 Structure of each clause \[structure\]](structure.md) · prev: [16.4.1.2 Summary \[structure.summary\]](structure.summary.md) · next: [16.4.1.4 Detailed specifications \[structure.specifications\]](structure.specifications.md)

**1** Requirements describe constraints that shall be met by a C++ program that extends the standard library. Such extensions are generally one of the following:

- Template arguments
- Derived classes
- Containers, iterators, and algorithms that meet an interface convention or model a concept

**2** The string and iostream components use an explicit representation of operations required of template arguments. They use a class template `char_traits` to define these constraints.

**3** Interface convention requirements are stated as generally as possible. Instead of stating “class `X` has to define a member function `operator++()`”, the interface requires “for any object `x` of class `X`, `++x` is defined”. That is, whether the operator is a member is unspecified.

**4** Requirements are stated in terms of well-defined expressions that define valid terms of the types that meet the requirements. For every set of well-defined expression requirements there is either a named concept or a table that specifies an initial set of the valid expressions and their semantics. Any generic algorithm ([[algorithms]](../algorithms/algorithms.md)) that uses the well-defined expression requirements is described in terms of the valid expressions for its template type parameters.

**5** The library specification uses a typographical convention for naming requirements. Names in *italic* type that begin with the prefix *Cpp17* refer to sets of well-defined expression requirements typically presented in tabular form, possibly with additional prose semantic requirements. For example, *Cpp17Destructible* ([Table 32](utility.arg.requirements.md#tab:cpp17.destructible)) is such a named requirement. Names in `constant width` type refer to library concepts which are presented as a concept definition ([[temp]](../../temp/temp.md)), possibly with additional prose semantic requirements. For example, `destructible` ([[concept.destructible]](../concepts/concept.destructible.md)) is such a named requirement.

**6** Template argument requirements are sometimes referenced by name. See [[type.descriptions]](type.descriptions.md).

**7** In some cases the semantic requirements are presented as C++ code. Such code is intended as a specification of equivalence of a construct to another construct, not necessarily as the way the construct must be implemented.[^1]

**8** Required operations of any concept defined in this document need not be total functions; that is, some arguments to a required operation may result in the required semantics failing to be met. [*Example 1*: The required `<` operator of the `totally_ordered` concept ([[concept.totallyordered]](../concepts/concept.totallyordered.md)) does not meet the semantic requirements of that concept when operating on NaNs. — *end example*] This does not affect whether a type models the concept.

**9** A declaration may explicitly impose requirements through its associated constraints ([[temp.constr.decl]](../../temp/temp.constr.decl.md)). When the associated constraints refer to a concept ([[temp.concept]](../../temp/temp.concept.md)), the semantic constraints specified for that concept are additionally imposed on the use of the declaration.

[^1]: Although in some cases the code given is unambiguously the optimum implementation.
