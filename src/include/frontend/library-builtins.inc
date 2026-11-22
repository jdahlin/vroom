/* ISO C and POSIX library prototypes used by both frontends.
 * LIBRARY_BUILTIN takes the library name, C builtin id, C flags, signature,
 * and C++ noreturn flag. The signature gives the result first, then parameters.
 * v = void, i = int, z = size_t, p/s = void or char pointer (uppercase for const
 * pointees), and a trailing dot marks a variadic function.
 */
LIBRARY_BUILTIN(abort, CC_BUILTIN_LIBRARY, 0, "v", 1)
LIBRARY_BUILTIN(abs, CC_BUILTIN_LIBRARY, 1, "ii", 0)
LIBRARY_BUILTIN(ffs, CC_BUILTIN_FFS, 1, "ii", 0)
LIBRARY_BUILTIN(memcmp, CC_BUILTIN_LIBRARY, 0, "iPPz", 0)
LIBRARY_BUILTIN(memcpy, CC_BUILTIN_LIBRARY, (CC_LIBRARY_KIND_MEMCPY << 1) | 0, "ppPz", 0)
LIBRARY_BUILTIN(memmove, CC_BUILTIN_LIBRARY, (CC_LIBRARY_KIND_MEMMOVE << 1) | 0, "ppPz", 0)
LIBRARY_BUILTIN(memset, CC_BUILTIN_LIBRARY, (CC_LIBRARY_KIND_MEMSET << 1) | 0, "ppiz", 0)
LIBRARY_BUILTIN(printf, CC_BUILTIN_LIBRARY, 0, "iS.", 0)
LIBRARY_BUILTIN(puts, CC_BUILTIN_LIBRARY, 0, "iS", 0)
LIBRARY_BUILTIN(snprintf, CC_BUILTIN_LIBRARY, 0, "iszS.", 0)
LIBRARY_BUILTIN(sprintf, CC_BUILTIN_LIBRARY, 0, "isS.", 0)
LIBRARY_BUILTIN(strchr, CC_BUILTIN_LIBRARY, 0, "sSi", 0)
LIBRARY_BUILTIN(strcmp, CC_BUILTIN_LIBRARY, (CC_LIBRARY_KIND_STRCMP << 1) | 0, "iSS", 0)
LIBRARY_BUILTIN(strcpy, CC_BUILTIN_LIBRARY, 0, "ssS", 0)
LIBRARY_BUILTIN(strlen, CC_BUILTIN_STRLEN, 1, "zS", 0)
LIBRARY_BUILTIN(strncmp, CC_BUILTIN_LIBRARY, 0, "iSSz", 0)
LIBRARY_BUILTIN(strncpy, CC_BUILTIN_LIBRARY, 0, "ssSz", 0)
