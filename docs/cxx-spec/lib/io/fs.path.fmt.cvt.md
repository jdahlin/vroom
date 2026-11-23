# 29.11.7.2.1 Argument format conversions [fs.path.fmt.cvt]

[Contents](../../README.md) · up: [29.11.7.2 Conversions \[fs.path.cvt\]](fs.path.cvt.md) · prev: [29.11.7.2 Conversions \[fs.path.cvt\]](fs.path.cvt.md) · next: [29.11.7.2.2 Type and encoding conversions \[fs.path.type.cvt\]](fs.path.type.cvt.md)

**1** [*Note 1*: The format conversions described in this subclause are not applied on POSIX-based operating systems because on these systems:

- The generic format is acceptable as a native path.
- There is no need to distinguish between native format and generic format in function arguments.
- Paths for regular files and paths for directories share the same syntax.

— *end note*]

**2** Several functions are defined to accept *detected-format* arguments, which are character sequences. A detected-format argument represents a path using either a pathname in the generic format ([[fs.path.generic]](fs.path.generic.md)) or a pathname in the native format ([[fs.class.path]](fs.class.path.md)). Such an argument is taken to be in the generic format if and only if it matches the generic format and is not acceptable to the operating system as a native path.

**3** [*Note 2*: Some operating systems may have no unambiguous way to distinguish between native format and generic format arguments. This is by design as it simplifies use for operating systems that do not require disambiguation. An implementation for an operating system where disambiguation is required is permitted to distinguish between the formats. — *end note*]

**4** Pathnames are converted as needed between the generic and native formats in an operating-system-dependent manner. Let *G(n)* and *N(g)* in a mathematical sense be the implementation’s functions that convert native-to-generic and generic-to-native formats respectively. If *g=G(n)* for some *n*, then *G(N(g))=g*; if *n=N(g)* for some *g*, then *N(G(n))=n*. [*Note 3*: Neither *G* nor *N* need be invertible. — *end note*]

**5** If the native format requires paths for regular files to be formatted differently from paths for directories, the path shall be treated as a directory path if its last element is a *directory-separator*, otherwise it shall be treated as a path to a regular file.

**6** [*Note 4*: A path stores a native format pathname ([[fs.path.native.obs]](fs.path.native.obs.md)) and acts as if it also stores a generic format pathname, related as given below. The implementation may generate the generic format pathname based on the native format pathname (and possibly other information) when requested. — *end note*]

**7** When a path is constructed from or is assigned a single representation separate from any path, the other representation is selected by the appropriate conversion function (*G* or *N*).

**8** When the (new) value *p* of one representation of a path is derived from the representation of that or another path, a value *q* is chosen for the other representation. The value *q* converts to *p* (by *G* or *N* as appropriate) if any such value does so; *q* is otherwise unspecified. [*Note 5*: If *q* is the result of converting any path at all, it is the result of converting *p*. — *end note*]
