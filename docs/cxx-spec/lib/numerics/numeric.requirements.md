# 26.2 Numeric type requirements [numeric.requirements]

[Contents](../../README.md) · up: [26 Numerics library \[numerics\]](numerics.md) · prev: [26.1 General \[numerics.general\]](numerics.general.md) · next: [26.3 The floating-point environment \[cfenv\]](cfenv.md)

**1** The `complex` and `valarray` components are parameterized by the type of information they contain and manipulate. A C++ program shall instantiate these components only with a numeric type. A *numeric type* is a cv-unqualified object type `T` that meets the *Cpp17DefaultConstructible*, *Cpp17CopyConstructible*, *Cpp17CopyAssignable*, and *Cpp17Destructible* requirements ([[utility.arg.requirements]](../library/utility.arg.requirements.md)).[^1]

**2** If any operation on `T` throws an exception the effects are undefined.

**3** In addition, many member and related functions of `valarray<T>` can be successfully instantiated and will exhibit well-defined behavior if and only if `T` meets additional requirements specified for each such member or related function.

**4** [*Example 1*: It is valid to instantiate `valarray<complex>`, but `operator>()` will not be successfully instantiated for `valarray<complex>` operands, since `complex` does not have any ordering operators. — *end example*]

[^1]: In other words, value types. These include arithmetic types, pointers, the library class `complex`, and instantiations of `valarray` for value types.
