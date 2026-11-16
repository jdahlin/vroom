# 12.4.1.7 Initialization by list-initialization [over.match.list]

[Contents](README.md) · up: [12.4.1 Candidate functions and argument lists \[over.match.funcs\]](over.match.funcs.md) · prev: [12.4.1.6 Initialization by conversion function for direct reference binding \[over.match.ref\]](over.match.ref.md) · next: [12.4.1.8 Class template argument deduction \[over.match.class.deduct\]](over.match.class.deduct.md)

**1** When objects of non-aggregate class type `T` are list-initialized such that [[dcl.init.list]](dcl.init.list.md) specifies that overload resolution is performed according to the rules in this subclause or when forming a list-initialization sequence according to [[over.ics.list]](over.ics.list.md), overload resolution selects the constructor in two phases:

- If the initializer list is not empty or `T` has no default constructor, overload resolution is first performed where the candidate functions are the initializer-list constructors ([[dcl.init.list]](dcl.init.list.md)) of the class `T` and the argument list consists of the initializer list as a single argument.
- Otherwise, or if no viable initializer-list constructor is found, overload resolution is performed again, where the candidate functions are all the constructors of the class `T` and the argument list consists of the elements of the initializer list.

In copy-list-initialization, if an explicit constructor is chosen, the initialization is ill-formed. [*Note 1*: This differs from other situations ([[over.match.ctor]](over.match.ctor.md), [[over.match.copy]](over.match.copy.md)), where only converting constructors are considered for copy-initialization. This restriction only applies if this initialization is part of the final result of overload resolution. — *end note*]
