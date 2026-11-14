/*
 * vcc's <stdbool.h> (C11 7.18, C23 7.21), for targets whose sysroot has none
 * (Windows x64: mingw-w64 leaves it to the compiler).  In C23 bool, true and
 * false are keywords and the header only says it is there.
 */
#ifndef __VCC_STDBOOL_H
#define __VCC_STDBOOL_H

#define __bool_true_false_are_defined 1

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#define bool _Bool
#define true 1
#define false 0
#endif

#endif
