# 19.2 Exception classes [std.exceptions]

[Contents](../../README.md) · up: [19 Diagnostics library \[diagnostics\]](diagnostics.md) · prev: [19.1 General \[diagnostics.general\]](diagnostics.general.md) · next: [19.2.1 Header `<stdexcept>` synopsis \[stdexcept.syn\]](stdexcept.syn.md)

**1** The C++ standard library provides classes to be used to report certain errors ([[res.on.exception.handling]](../library/res.on.exception.handling.md)) in C++ programs. In the error model reflected in these classes, errors are divided into two broad categories: *logic* errors and *runtime* errors.

**2** The distinguishing characteristic of logic errors is that they are due to errors in the internal logic of the program. In theory, they are preventable.

**3** By contrast, runtime errors are due to events beyond the scope of the program. They cannot be easily predicted in advance. The header `<stdexcept>` defines several types of predefined exceptions for reporting errors in a C++ program. These exceptions are related by inheritance.

## Subclauses

- [19.2.1 Header `<stdexcept>` synopsis \[stdexcept.syn\]](stdexcept.syn.md)
- [19.2.2 Class `logic_error` \[logic.error\]](logic.error.md)
- [19.2.3 Class `domain_error` \[domain.error\]](domain.error.md)
- [19.2.4 Class `invalid_argument` \[invalid.argument\]](invalid.argument.md)
- [19.2.5 Class `length_error` \[length.error\]](length.error.md)
- [19.2.6 Class `out_of_range` \[out.of.range\]](out.of.range.md)
- [19.2.7 Class `runtime_error` \[runtime.error\]](runtime.error.md)
- [19.2.8 Class `range_error` \[range.error\]](range.error.md)
- [19.2.9 Class `overflow_error` \[overflow.error\]](overflow.error.md)
- [19.2.10 Class `underflow_error` \[underflow.error\]](underflow.error.md)
