# C.3.6 [[input.output]](../lib/io/input.output.md): input/output library [diff.cpp11.input.output]

[Contents](../README.md) · up: [C.3 C++ and ISO C++ 2011 \[diff.cpp11\]](diff.cpp11.md) · prev: [C.3.5 [[library]](../lib/library/library.md): library introduction \[diff.cpp11.library\]](diff.cpp11.library.md) · next: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md)

**1** **Affected subclause:** [[c.files]](../lib/io/c.files.md) <br>**Change:** `gets` is not defined. <br>**Rationale:** Use of `gets` is considered dangerous. <br>**Effect on original feature:** Valid C++ 2011 code that uses the `gets` function may fail to compile in this International Standard.
