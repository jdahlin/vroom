/* the synthetic register target of the allocator's tests (synth.c) */
#ifndef VCC_VIR_SYNTH_H
#define VCC_VIR_SYNTH_H

#include "vir/alloc.h"

typedef struct vir_synth {
	vir_regs regs;
	uint32_t nai, naf;	/* argument registers per class */
	bool x86;
} vir_synth;

/* ni integer and nf float registers, the top ci and cf callee-saved; false (err) if below the minimum */
bool vir_synth_init(vir_synth *t, uint32_t ni, uint32_t nf, uint32_t ci, uint32_t cf, bool x86, char *err, size_t en);

#endif
