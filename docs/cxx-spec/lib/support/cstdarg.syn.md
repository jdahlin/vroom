# 17.13.1 Header `<cstdarg>` synopsis [cstdarg.syn]

[Contents](../../README.md) · up: [17.13 Other runtime support \[support.runtime\]](support.runtime.md) · prev: [17.13 Other runtime support \[support.runtime\]](support.runtime.md) · next: [17.13.2 Header `<csetjmp>` synopsis \[csetjmp.syn\]](csetjmp.syn.md)

```cpp
namespace std {
  using va_list = see below;
}

#define va_arg(V, P) see below
#define va_copy(VDST, VSRC) see below
#define va_end(V) see below
#define va_start(V, P) see below
```

**1** The contents of the header `<cstdarg>` are the same as the C standard library header `<stdarg.h>`, with the following changes: The restrictions that ISO C places on the second parameter to the `va_start` macro in header `<stdarg.h>` are different in this document. The parameter `parmN` is the rightmost parameter in the variable parameter list of the function definition (the one just before the `...`).[^1] If the parameter `parmN` is a pack expansion ([[temp.variadic]](../../temp/temp.variadic.md)) or an entity resulting from a lambda capture ([[expr.prim.lambda]](../../expr/expr.prim.lambda.md)), the program is ill-formed, no diagnostic required. If the parameter `parmN` is of a reference type, or of a type that is not compatible with the type that results when passing an argument for which there is no parameter, the behavior is undefined.

*See also:* ISO C 7.16.1.1

[^1]: Note that `va_start` is required to work as specified even if unary `operator&` is overloaded for the type of `parmN`.
