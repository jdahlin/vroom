# 14.6 Special functions [except.special]

[Contents](../README.md) · up: [14 Exception handling \[except\]](except.md) · prev: [14.5 Exception specifications \[except.spec\]](except.spec.md) · next: [14.6.1 The `std::terminate` function \[except.terminate\]](except.terminate.md)

**1** The function `std::terminate` ([[except.terminate]](except.terminate.md)) is used by the exception handling mechanism for coping with errors related to the exception handling mechanism itself. The function `std::current_exception()` ([[propagation]](../lib/support/propagation.md)) and the class `std::nested_exception` ([[except.nested]](../lib/support/except.nested.md)) can be used by a program to capture the currently handled exception.

## Subclauses

- [14.6.1 The `std::terminate` function \[except.terminate\]](except.terminate.md)
- [14.6.2 The `std::uncaught_exceptions()` function \[except.uncaught\]](except.uncaught.md)
