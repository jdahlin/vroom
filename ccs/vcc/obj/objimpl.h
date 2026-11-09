/* the files of the object writer (obj/, arch/<cpu>/obj/, platform/<os>/) share this header */
#ifndef VCC_OBJIMPL_H
#define VCC_OBJIMPL_H

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "obj.h"
#include "../lib/hash.h"

/* functions one file defines and another calls: static when the module is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

static inline void *reserve(size_t n) { return vreserve(n, MAP_NORESERVE, "vcc: elf: mmap"); }
static inline void *rpush(vregion *r, size_t n, size_t al) { return vregion_push(r, n, al, "vcc: elf: region full"); }

static inline uint64_t symoff(ElfObj *o, ElfSym *y) { return elf_frag(o, y->frag)->base + y->value; }
static inline uint32_t symsec(ElfObj *o, ElfSym *y) { return elf_frag(o, y->frag)->sec; }
static inline int insec(ElfSym *y) { return (y->flags & (ESYM_DEFINED | ESYM_ABS)) == ESYM_DEFINED; }
static inline void put(uint8_t *p, uint64_t v, int n) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> 8 * i); }

/* the object file writer's relocation output (platform/linux/elf.c) */
typedef struct Out Out;

/* arch/<cpu>/obj/fixup.c: a fixup resolved in place or turned into a relocation */
VPRIV void elf_resolve(ElfObj *o, Out *w, ElfFix *x);
/* platform/linux/elf.c: a relocation record for fixup x */
VPRIV void elf_reloc(ElfObj *o, Out *w, ElfFix *x, uint64_t P, uint32_t type, ElfSym *a, uint32_t aid, int64_t add);

#endif
