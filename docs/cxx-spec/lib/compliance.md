# 16.5.1.3 Freestanding implementations [compliance]

[Contents](../README.md) · up: [16.5.1 Library contents and organization \[organization\]](organization.md) · prev: [16.5.1.2 Headers \[headers\]](headers.md) · next: [16.5.2 Using the library \[using\]](using.md)

**1** Two kinds of implementations are defined: *hosted* and *freestanding* ([[intro.compliance]](../intro.compliance.md)); the kind of the implementation is implementation-defined. For a hosted implementation, this document describes the set of available headers.

**2** A freestanding implementation has an implementation-defined set of headers. This set shall include at least the headers shown in [Table 24](compliance.md#tab:headers.cpp.fs).

<a id="tab:headers.cpp.fs"></a>
**Table 24: C++ headers for freestanding implementations** [tab:headers.cpp.fs]

|  |  |  |
| --- | --- | --- |
| [[support.types]](support.types.md) | Types | `<cstddef>` |
| [[support.limits]](support.limits.md) | Implementation properties | `<cfloat>`, `<climits>`, `<limits>`, `<version>` |
| [[cstdint]](cstdint.md) | Integer types | `<cstdint>` |
| [[support.start.term]](support.start.term.md) | Start and termination | `<cstdlib>` |
| [[support.dynamic]](support.dynamic.md) | Dynamic memory management | `<new>` |
| [[support.rtti]](support.rtti.md) | Type identification | `<typeinfo>` |
| [[support.srcloc]](support.srcloc.md) | Source location | `<source_location>` |
| [[support.exception]](support.exception.md) | Exception handling | `<exception>` |
| [[support.initlist]](support.initlist.md) | Initializer lists | `<initializer_list>` |
| [[cmp]](cmp.md) | Comparisons | `<compare>` |
| [[support.coroutine]](support.coroutine.md) | Coroutines support | `<coroutine>` |
| [[support.runtime]](support.runtime.md) | Other runtime support | `<cstdarg>` |
| [[concepts]](concepts.md) | Concepts library | `<concepts>` |
| [[meta]](meta.md) | Type traits | `<type_traits>` |
| [[bit]](bit.md) | Bit manipulation | `<bit>` |
| [[atomics]](atomics.md) | Atomics | `<atomic>` |

**3** The supplied version of the header `<cstdlib>` ([[cstdlib.syn]](cstdlib.syn.md)) shall declare at least the functions `abort`, `atexit`, `at_quick_exit`, `exit`, and `quick_exit` ([[support.start.term]](support.start.term.md)). The supplied version of the header `<atomic>` ([[atomics.syn]](atomics.syn.md)) shall meet the same requirements as for a hosted implementation except that support for always lock-free integral atomic types ([[atomics.lockfree]](atomics.lockfree.md)) is implementation-defined, and whether or not the type aliases `atomic_signed_lock_free` and `atomic_unsigned_lock_free` are defined ([[atomics.alias]](atomics.alias.md)) is implementation-defined. The other headers listed in this table shall meet the same requirements as for a hosted implementation.
