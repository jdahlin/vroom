/* the synthetic register target of the allocator's tests (synthetic-target.c) */
#ifndef VCC_VIR_SYNTH_H
#define VCC_VIR_SYNTH_H

#include "vir/register-allocation.h"

typedef struct vir_synthetic_target {
	vir_target_registers registers;
	uint32_t allocatable_integer_count, allocatable_float_count;	/* argument registers per class */
	bool is_x86;
} vir_synthetic_target;

/* ni integer and nf float registers, the top ci and cf callee-saved; false (err) if below the minimum */
bool vir_synth_init(vir_synthetic_target *t, uint32_t ni, uint32_t nf, uint32_t ci, uint32_t cf, bool x86, char *err, size_t en);

#endif
