# 16.5.2.3 Linkage [using.linkage]

[Contents](../README.md) · up: [16.5.2 Using the library \[using\]](using.md) · prev: [16.5.2.2 Headers \[using.headers\]](using.headers.md) · next: [16.5.3 Requirements on types and expressions \[utility.requirements\]](utility.requirements.md)

**1** Entities in the C++ standard library have external linkage ([[basic.link]](../basic.link.md)). Unless otherwise specified, objects and functions have the default `extern "C++"` linkage ([[dcl.link]](../dcl.link.md)).

**2** Whether a name from the C standard library declared with external linkage has `extern "C"` or `extern "C++"` linkage is implementation-defined. It is recommended that an implementation use `extern "C++"` linkage for this purpose.[^1]

**3** Objects and functions defined in the library and required by a C++ program are included in the program prior to program startup.

**4** See also replacement functions ([[replacement.functions]](replacement.functions.md)), runtime changes ([[handler.functions]](handler.functions.md)).

[^1]: The only reliable way to declare an object or function signature from the C standard library is by including the header that declares it, notwithstanding the latitude granted in 7.1.4 of the C Standard.
