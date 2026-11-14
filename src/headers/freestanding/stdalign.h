/*
 * vcc's <stdalign.h> (C11 7.15; obsolescent in C23, where alignas and alignof
 * are keywords).
 */
#ifndef __VCC_STDALIGN_H
#define __VCC_STDALIGN_H

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#define alignas _Alignas
#define alignof _Alignof
#endif
#define __alignas_is_defined 1
#define __alignof_is_defined 1

#endif
