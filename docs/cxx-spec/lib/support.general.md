# 17.1 General [support.general]

[Contents](../README.md) · up: [17 Language support library \[support\]](support.md) · prev: [17 Language support library \[support\]](support.md) · next: [17.2 Common definitions \[support.types\]](support.types.md)

**1** This Clause describes the function signatures that are called implicitly, and the types of objects generated implicitly, during the execution of some C++ programs. It also describes the headers that declare these function signatures and define any related types.

**2** The following subclauses describe common type definitions used throughout the library, characteristics of the predefined types, functions supporting start and termination of a C++ program, support for dynamic memory management, support for dynamic type identification, support for exception processing, support for initializer lists, and other runtime support, as summarized in [Table 37](support.general.md#tab:support.summary).

<a id="tab:support.summary"></a>
**Table 37: Language support library summary** [tab:support.summary]

|  |  |  |
| --- | --- | --- |
| [[support.types]](support.types.md) | Common definitions | `<cstddef>`, `<cstdlib>` |
| [[support.limits]](support.limits.md) | Implementation properties | `<cfloat>`, `<climits>`, `<limits>`, `<version>` |
| [[cstdint]](cstdint.md) | Integer types | `<cstdint>` |
| [[support.start.term]](support.start.term.md) | Start and termination | `<cstdlib>` |
| [[support.dynamic]](support.dynamic.md) | Dynamic memory management | `<new>` |
| [[support.rtti]](support.rtti.md) | Type identification | `<typeinfo>` |
| [[support.srcloc]](support.srcloc.md) | Source location | `<source_location>` |
| [[support.exception]](support.exception.md) | Exception handling | `<exception>` |
| [[support.initlist]](support.initlist.md) | Initializer lists | `<initializer_list>` |
| [[cmp]](cmp.md) | Comparisons | `<compare>` |
| [[support.coroutine]](support.coroutine.md) | Coroutines | `<coroutine>` |
| [[support.runtime]](support.runtime.md) | Other runtime support | `<csetjmp>`, `<csignal>`, `<cstdarg>`, `<cstdlib>` |
