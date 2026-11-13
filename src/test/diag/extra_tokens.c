/* diag-flags: -std=c23 */
#define A 1
#ifdef A junk
#endif junk
#ifndef B x y
#else z
#endif
#undef A q
#include "h.h" w
#if 1
#elif 1
#endif
#ifdef A
#elifdef B c
#endif
#line 5 "f.c" x
int v;
#if 0
#else a
#endif b
#if 1
#else c
#endif
#if 0
#ifdef X d
#endif e
#else f
#endif
#if 0
#elif 1 g
#endif
#if 0
#elifdef X i
#endif
