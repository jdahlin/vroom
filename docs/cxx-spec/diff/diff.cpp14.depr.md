# C.2.11 [[depr]](../depr/depr.md): compatibility features [diff.cpp14.depr]

[Contents](../README.md) · up: [C.2 C++ and ISO C++ 2014 \[diff.cpp14\]](diff.cpp14.md) · prev: [C.2.10 [[containers]](../lib/containers/containers.md): containers library \[diff.cpp14.containers\]](diff.cpp14.containers.md) · next: [C.3 C++ and ISO C++ 2011 \[diff.cpp11\]](diff.cpp11.md)

**1** **Change:** The class templates `auto_ptr`, `unary_function`, and `binary_function`, the function templates `random_shuffle`, and the function templates (and their return types) `ptr_fun`, `mem_fun`, `mem_fun_ref`, `bind1st`, and `bind2nd` are not defined. <br>**Rationale:** Superseded by new features. <br>**Effect on original feature:** Valid C++ 2014 code that uses these class templates and function templates may fail to compile in this International Standard.

**2** **Change:** Remove old iostreams members [depr.ios.members]. <br>**Rationale:** Redundant feature for compatibility with pre-standard code has served its time. <br>**Effect on original feature:** A valid C++ 2014 program using these identifiers may be ill-formed in this International Standard.
