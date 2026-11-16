# C.4.13 [[numerics]](lib/numerics.md): numerics library [diff.cpp03.numerics]

[Contents](README.md) · up: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · prev: [C.4.12 [[algorithms]](lib/algorithms.md): algorithms library \[diff.cpp03.algorithms\]](diff.cpp03.algorithms.md) · next: [C.4.14 [[input.output]](lib/input.output.md): input/output library \[diff.cpp03.input.output\]](diff.cpp03.input.output.md)

**1** **Affected subclause:** [[complex.numbers]](lib/complex.numbers.md) <br>**Change:** Specified representation of complex numbers. <br>**Rationale:** Compatibility with C99. <br>**Effect on original feature:** Valid C++ 2003 code that uses implementation-specific knowledge about the binary representation of the required template specializations of `std::complex` may not be compatible with this International Standard.
