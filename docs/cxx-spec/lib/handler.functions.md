# 16.5.4.7 Handler functions [handler.functions]

[Contents](../README.md) · up: [16.5.4 Constraints on programs \[constraints\]](constraints.md) · prev: [16.5.4.6 Replacement functions \[replacement.functions\]](replacement.functions.md) · next: [16.5.4.8 Other functions \[res.on.functions\]](res.on.functions.md)

**1** The C++ standard library provides a default version of the following handler function ([[support]](support.md)):

- `terminate_handler`

**2** A C++ program may install different handler functions during execution, by supplying a pointer to a function defined in the program or the library as an argument to (respectively):

- `set_new_handler`
- `set_terminate`

See also subclauses [[alloc.errors]](alloc.errors.md), Storage allocation errors, and [[support.exception]](support.exception.md), Exception handling.

**3** A C++ program can get a pointer to the current handler function by calling the following functions:

- `get_new_handler`
- `get_terminate`

**4** Calling the `set_*` and `get_*` functions shall not incur a data race. A call to any of the `set_*` functions shall synchronize with subsequent calls to the same `set_*` function and to the corresponding `get_*` function.
