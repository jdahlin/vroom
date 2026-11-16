# 16.5.4.9 Function arguments [res.on.arguments]

[Contents](../../README.md) · up: [16.5.4 Constraints on programs \[constraints\]](constraints.md) · prev: [16.5.4.8 Other functions \[res.on.functions\]](res.on.functions.md) · next: [16.5.4.10 Library object access \[res.on.objects\]](res.on.objects.md)

**1** Each of the following applies to all arguments to functions defined in the C++ standard library, unless explicitly stated otherwise.

- If an argument to a function has an invalid value (such as a value outside the domain of the function or a pointer invalid for its intended use), the behavior is undefined.
- If a function argument is described as being an array, the pointer actually passed to the function shall have a value such that all address computations and accesses to objects (that would be valid if the pointer did point to the first element of such an array) are in fact valid.
- If a function argument binds to an rvalue reference parameter, the implementation may assume that this parameter is a unique reference to this argument. [*Note 1*: If the parameter is a generic parameter of the form `T&&` and an lvalue of type `A` is bound, the argument binds to an lvalue reference ([[temp.deduct.call]](../../temp/temp.deduct.call.md)) and thus is not covered by the previous sentence. — *end note*] [*Note 2*: If a program casts an lvalue to an xvalue while passing that lvalue to a library function (e.g., by calling the function with the argument `std::move(x)`), the program is effectively asking that function to treat that lvalue as a temporary object. The implementation is free to optimize away aliasing checks which might be needed if the argument was an lvalue. — *end note*]
