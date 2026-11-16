# 16.5.4.3.3 External linkage [extern.names]

[Contents](../../README.md) · up: [16.5.4.3 Reserved names \[reserved.names\]](reserved.names.md) · prev: [16.5.4.3.2 Macro names \[macro.names\]](macro.names.md) · next: [16.5.4.3.4 Types \[extern.types\]](extern.types.md)

**1** Each name declared as an object with external linkage in a header is reserved to the implementation to designate that library object with external linkage,[^1] both in namespace `std` and in the global namespace.

**2** Each global function signature declared with external linkage in a header is reserved to the implementation to designate that function signature with external linkage.[^2]

**3** Each name from the C standard library declared with external linkage is reserved to the implementation for use as a name with `extern "C"` linkage, both in namespace `std` and in the global namespace.

**4** Each function signature from the C standard library declared with external linkage is reserved to the implementation for use as a function signature with both `extern "C"` and `extern "C++"` linkage,[^3] or as a name of namespace scope in the global namespace.

[^1]: The list of such reserved names includes `errno`, declared or defined in `<cerrno>` ([[cerrno.syn]](../diagnostics/cerrno.syn.md)).
[^2]: The list of such reserved function signatures with external linkage includes `setjmp(jmp_buf)`, declared or defined in `<csetjmp>` ([[csetjmp.syn]](../support/csetjmp.syn.md)), and `va_end(va_list)`, declared or defined in `<cstdarg>` ([[cstdarg.syn]](../support/cstdarg.syn.md)).
[^3]: The function signatures declared in `<cuchar>` ([[cuchar.syn]](../strings/cuchar.syn.md)), `<cwchar>` ([[cwchar.syn]](../strings/cwchar.syn.md)), and `<cwctype>` ([[cwctype.syn]](../strings/cwctype.syn.md)) are always reserved, notwithstanding the restrictions imposed in subclause 4.5.1 of Amendment 1 to the C Standard for these headers.
