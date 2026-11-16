# 16.5.4.2.1 Namespace `std` [namespace.std]

[Contents](../README.md) · up: [16.5.4.2 Namespace use \[namespace.constraints\]](namespace.constraints.md) · prev: [16.5.4.2 Namespace use \[namespace.constraints\]](namespace.constraints.md) · next: [16.5.4.2.2 Namespace `posix` \[namespace.posix\]](namespace.posix.md)

**1** Unless otherwise specified, the behavior of a C++ program is undefined if it adds declarations or definitions to namespace `std` or to a namespace within namespace `std`.

**2** Unless explicitly prohibited, a program may add a template specialization for any standard library class template to namespace `std` provided that (a) the added declaration depends on at least one program-defined type and (b) the specialization meets the standard library requirements for the original template.[^1]

**3** The behavior of a C++ program is undefined if it declares an explicit or partial specialization of any standard library variable template, except where explicitly permitted by the specification of that variable template.

**4** The behavior of a C++ program is undefined if it declares

- an explicit specialization of any member function of a standard library class template, or
- an explicit specialization of any member function template of a standard library class or class template, or
- an explicit or partial specialization of any member class template of a standard library class or class template, or
- a deduction guide for any standard library class template.

**5** A program may explicitly instantiate a class template defined in the standard library only if the declaration (a) depends on the name of at least one program-defined type and (b) the instantiation meets the standard library requirements for the original template.

**6** Let `F` denote a standard library function ([[global.functions]](global.functions.md)), a standard library static member function, or an instantiation of a standard library function template. Unless `F` is designated an *addressable function*, the behavior of a C++ program is unspecified (possibly ill-formed) if it explicitly or implicitly attempts to form a pointer to `F`. [*Note 1*: Possible means of forming such pointers include application of the unary `&` operator ([[expr.unary.op]](../expr.unary.op.md)), `addressof` ([[specialized.addressof]](specialized.addressof.md)), or a function-to-pointer standard conversion ([[conv.func]](../conv.func.md)). — *end note*] Moreover, the behavior of a C++ program is unspecified (possibly ill-formed) if it attempts to form a reference to `F` or if it attempts to form a pointer-to-member designating either a standard library non-static member function ([[member.functions]](member.functions.md)) or an instantiation of a standard library member function template.

**7** Other than in namespace `std` or in a namespace within namespace `std`, a program may provide an overload for any library function template designated as a *customization point*, provided that (a) the overload’s declaration depends on at least one user-defined type and (b) the overload meets the standard library requirements for the customization point.[^2] [*Note 2*: This permits a (qualified or unqualified) call to the customization point to invoke the most appropriate overload for the given arguments. — *end note*]

**8** A translation unit shall not declare namespace `std` to be an inline namespace ([[namespace.def]](../namespace.def.md)).

[^1]: Any library code that instantiates other library templates must be prepared to work adequately with any user-supplied specialization that meets the minimum requirements of this document.
[^2]: Any library customization point must be prepared to work adequately with any user-defined overload that meets the minimum requirements of this document. Therefore an implementation may elect, under the as-if rule ([[intro.execution]](../intro.execution.md)), to provide any customization point in the form of an instantiated function object ([[function.objects]](function.objects.md)) even though the customization point’s specification is in the form of a function template. The template parameters of each such function object and the function parameters and return type of the object’s `operator()` must match those of the corresponding customization point’s specification.
