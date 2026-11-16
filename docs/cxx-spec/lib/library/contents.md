# 16.5.1.1 Library contents [contents]

[Contents](../../README.md) · up: [16.5.1 Library contents and organization \[organization\]](organization.md) · prev: [16.5.1 Library contents and organization \[organization\]](organization.md) · next: [16.5.1.2 Headers \[headers\]](headers.md)

**1** The C++ standard library provides definitions for the entities and macros described in the synopses of the C++ standard library headers ([[headers]](headers.md)), unless otherwise specified.

**2** All library entities except `operator new` and `operator delete` are defined within the namespace `std` or namespaces nested within namespace `std`.[^1] It is unspecified whether names declared in a specific namespace are declared directly in that namespace or in an inline namespace inside that namespace.[^2]

**3** Whenever a name `x` defined in the standard library is mentioned, the name `x` is assumed to be fully qualified as `::std::x`, unless explicitly described otherwise. For example, if the *Effects:* element for library function `F` is described as calling library function `G`, the function `::std::G` is meant.

[^1]: The C standard library headers ([[depr.c.headers]](../../depr/depr.c.headers.md)) also define names within the global namespace, while the C++ headers for C library facilities ([[headers]](headers.md)) may also define names within the global namespace.
[^2]: This gives implementers freedom to use inline namespaces to support multiple configurations of the library.
