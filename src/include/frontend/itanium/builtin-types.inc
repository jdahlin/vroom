/*
 * The encodings of the fundamental types, the one table of them.
 * [abi:mangling-builtin]:
 *
 *     <builtin-type> ::= v
 *                    ::= w
 *                    ::= b
 *                    ::= c
 *                    ::= a
 *                    ::= h
 *                    ::= s
 *                    ::= t
 *                    ::= i
 *                    ::= j
 *                    ::= l
 *                    ::= m
 *                    ::= x
 *                    ::= y
 *                    ::= n
 *                    ::= o
 *                    ::= f
 *                    ::= d
 *                    ::= e
 *                    ::= g
 *                    ::= Di
 *                    ::= Ds
 *                    ::= Du
 *                    ::= Dn
 *                    ::= DF <number> _
 *                    ::= DF16b
 *
 * In the order of the section's table: void, wchar_t, bool, char, signed
 * char, unsigned char, short, unsigned short, int, unsigned int, long,
 * unsigned long, long long, unsigned long long, __int128, unsigned
 * __int128, float, double, long double, __float128, char32_t, char16_t,
 * char8_t, std::nullptr_t; _Float16 is DF16_ (the DF <number> _ form) and
 * __bf16 DF16b ([impl.clang] on x86-64; arm64's is AAPCS64's vendor type,
 * builtin_type).  _BitInt(N) is type_kind::bit_int's (DB, DU: type()).
 *
 * ITANIUM_BUILTIN_TYPE(fundamental_type, code)
 */
ITANIUM_BUILTIN_TYPE(void_, "v")
ITANIUM_BUILTIN_TYPE(wchar_t_, "w")
ITANIUM_BUILTIN_TYPE(bool_, "b")
ITANIUM_BUILTIN_TYPE(char_, "c")
ITANIUM_BUILTIN_TYPE(signed_char, "a")
ITANIUM_BUILTIN_TYPE(unsigned_char, "h")
ITANIUM_BUILTIN_TYPE(short_int, "s")
ITANIUM_BUILTIN_TYPE(unsigned_short_int, "t")
ITANIUM_BUILTIN_TYPE(int_, "i")
ITANIUM_BUILTIN_TYPE(unsigned_int, "j")
ITANIUM_BUILTIN_TYPE(long_int, "l")
ITANIUM_BUILTIN_TYPE(unsigned_long_int, "m")
ITANIUM_BUILTIN_TYPE(long_long_int, "x")
ITANIUM_BUILTIN_TYPE(unsigned_long_long_int, "y")
ITANIUM_BUILTIN_TYPE(int128, "n")
ITANIUM_BUILTIN_TYPE(unsigned_int128, "o")
ITANIUM_BUILTIN_TYPE(float_, "f")
ITANIUM_BUILTIN_TYPE(double_, "d")
ITANIUM_BUILTIN_TYPE(long_double, "e")
ITANIUM_BUILTIN_TYPE(float128, "g")
ITANIUM_BUILTIN_TYPE(char32_t_, "Di")
ITANIUM_BUILTIN_TYPE(char16_t_, "Ds")
ITANIUM_BUILTIN_TYPE(char8_t_, "Du")
ITANIUM_BUILTIN_TYPE(std_nullptr_t, "Dn")
ITANIUM_BUILTIN_TYPE(float16, "DF16_")
ITANIUM_BUILTIN_TYPE(bfloat16, "DF16b")
