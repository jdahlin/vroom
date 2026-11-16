# 16.4.2.2.6 Customization Point Object types [customization.point.object]

[Contents](../README.md) · up: [16.4.2.2 Type descriptions \[type.descriptions\]](type.descriptions.md) · prev: [16.4.2.2.5.2 Multibyte strings \[multibyte.strings\]](multibyte.strings.md) · next: [16.4.2.3 Functions within classes \[functions.within.classes\]](functions.within.classes.md)

**1** A *customization point object* is a function object ([[function.objects]](function.objects.md)) with a literal class type that interacts with program-defined types while enforcing semantic requirements on that interaction.

**2** The type of a customization point object, ignoring cv-qualifiers, shall model `semiregular` ([[concepts.object]](concepts.object.md)).

**3** All instances of a specific customization point object type shall be equal ([[concepts.equality]](concepts.equality.md)).

**4** The type `T` of a customization point object shall model `invocable<const T&, Args...>` ([[concept.invocable]](concept.invocable.md)) when the types in `Args...` meet the requirements specified in that customization point object’s definition. When the types of `Args...` do not meet the customization point object’s requirements, `T` shall not have a function call operator that participates in overload resolution.

**5** Each customization point object type constrains its return type to model a particular concept.

**6** [*Note 1*: Many of the customization point objects in the library evaluate function call expressions with an unqualified name which results in a call to a program-defined function found by argument dependent name lookup ([[basic.lookup.argdep]](../basic.lookup.argdep.md)). To preclude such an expression resulting in a call to unconstrained functions with the same name in namespace `std`, customization point objects specify that lookup for these expressions is performed in a context that includes deleted overloads matching the signatures of overloads defined in namespace `std`. When the deleted overloads are viable, program-defined overloads need be more specialized ([[temp.func.order]](../temp.func.order.md)) or more constrained ([[temp.constr.order]](../temp.constr.order.md)) to be used by a customization point object. — *end note*]
