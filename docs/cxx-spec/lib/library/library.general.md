# 16.1 General [library.general]

[Contents](../../README.md) · up: [16 Library introduction \[library\]](library.md) · prev: [16 Library introduction \[library\]](library.md) · next: [16.2 The C standard library \[library.c\]](library.c.md)

**1** This Clause describes the contents of the *C++ standard library*, how a well-formed C++ program makes use of the library, and how a conforming implementation may provide the entities in the library.

**2** The following subclauses describe the definitions ([[definitions]](definitions.md)), method of description ([[description]](description.md)), and organization ([[organization]](organization.md)) of the library. [[requirements]](requirements.md), [[support]](../support/support.md) through [[thread]](../thread/thread.md), and [[depr]](../../depr/depr.md) specify the contents of the library, as well as library requirements and constraints on both well-formed C++ programs and conforming implementations.

**3** Detailed specifications for each of the components in the library are in [[support]](../support/support.md)–[[thread]](../thread/thread.md), as shown in [Table 20](library.general.md#tab:library.categories).

<a id="tab:library.categories"></a>
**Table 20: Library categories** [tab:library.categories]

| Clause | Category |
| --- | --- |
| [[support]](../support/support.md) | Language support library |
| [[concepts]](../concepts/concepts.md) | Concepts library |
| [[diagnostics]](../diagnostics/diagnostics.md) | Diagnostics library |
| [[utilities]](../utilities/utilities.md) | General utilities library |
| [[strings]](../strings/strings.md) | Strings library |
| [[containers]](../containers/containers.md) | Containers library |
| [[iterators]](../iterators/iterators.md) | Iterators library |
| [[ranges]](../ranges/ranges.md) | Ranges library |
| [[algorithms]](../algorithms/algorithms.md) | Algorithms library |
| [[numerics]](../numerics/numerics.md) | Numerics library |
| [[time]](../time/time.md) | Time library |
| [[localization]](../localization/localization.md) | Localization library |
| [[input.output]](../io/input.output.md) | Input/output library |
| [[re]](../re/re.md) | Regular expressions library |
| [[atomics]](../atomics/atomics.md) | Atomic operations library |
| [[thread]](../thread/thread.md) | Thread support library |

**4** The language support library ([[support]](../support/support.md)) provides components that are required by certain parts of the C++ language, such as memory allocation ([[expr.new]](../../expr/expr.new.md), [[expr.delete]](../../expr/expr.delete.md)) and exception processing ([[except]](../../except/except.md)).

**5** The concepts library ([[concepts]](../concepts/concepts.md)) describes library components that C++ programs may use to perform compile-time validation of template arguments and perform function dispatch based on properties of types.

**6** The diagnostics library ([[diagnostics]](../diagnostics/diagnostics.md)) provides a consistent framework for reporting errors in a C++ program, including predefined exception classes.

**7** The general utilities library ([[utilities]](../utilities/utilities.md)) includes components used by other library elements, such as a predefined storage allocator for dynamic storage management ([[basic.stc.dynamic]](../../basic/basic.stc.dynamic.md)), and components used as infrastructure in C++ programs, such as tuples, function wrappers, and time facilities.

**8** The strings library ([[strings]](../strings/strings.md)) provides support for manipulating text represented as sequences of type `char`, sequences of type `char8_t`, sequences of type `char16_t`, sequences of type `char32_t`, sequences of type `wchar_t`, and sequences of any other character-like type.

**9** The localization library ([[localization]](../localization/localization.md)) provides extended internationalization support for text processing.

**10** The containers ([[containers]](../containers/containers.md)), iterators ([[iterators]](../iterators/iterators.md)), ranges ([[ranges]](../ranges/ranges.md)), and algorithms ([[algorithms]](../algorithms/algorithms.md)) libraries provide a C++ program with access to a subset of the most widely used algorithms and data structures.

**11** The numerics library ([[numerics]](../numerics/numerics.md)) provides numeric algorithms and complex number components that extend support for numeric processing. The `valarray` component provides support for *n*-at-a-time processing, potentially implemented as parallel operations on platforms that support such processing. The random number component provides facilities for generating pseudo-random numbers.

**12** The input/output library ([[input.output]](../io/input.output.md)) provides the `iostream` components that are the primary mechanism for C++ program input and output. They can be used with other elements of the library, particularly strings, locales, and iterators.

**13** The regular expressions library ([[re]](../re/re.md)) provides regular expression matching and searching.

**14** The atomic operations library ([[atomics]](../atomics/atomics.md)) allows more fine-grained concurrent access to shared data than is possible with locks.

**15** The thread support library ([[thread]](../thread/thread.md)) provides components to create and manage threads, including mutual exclusion and interthread communication.
