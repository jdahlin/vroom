/*
 * literal.h: VCC_LITERAL, a compound literal that compiles as C and as C++:
 * the aggregate values the shared library builds in expressions.
 */
#ifndef VCC_LITERAL_H
#define VCC_LITERAL_H

/* An aggregate value in expression context. The alias also accepts tagged
 * types such as struct iovec in C++ list initialization. */
#ifdef __cplusplus
template <typename T> using vcc_literal_type = T;
#define VCC_LITERAL(T, ...) (vcc_literal_type<T>{__VA_ARGS__})
#else
#define VCC_LITERAL(T, ...) ((T){__VA_ARGS__})
#endif

#endif
