/*
 * Known bits of integer values (doc/quality/DESIGN-vir-optimizer.md 6), the
 * analysis redundant-extensions reads.  For every value v of type i32 or i64,
 * of width w = 32 or 64, two small numbers, packed into out[v]:
 *
 *   z(v): v < 2^z as an unsigned w-bit number (z = w: nothing is known);
 *   s(v): v is the sign extension of its low s bits (s = w: nothing is known), s >= 1.
 *
 * Both are claims about every execution in which the instruction has no
 * immediate UB; UB flags (nsw, nuw, exact) are not used.  A value of another
 * type has z = s = 64.
 */
#ifndef VCC_VIR_KNOWN_BITS_H
#define VCC_VIR_KNOWN_BITS_H

#include "vir/pass.h"

static inline uint32_t vir_known_bits_z(uint32_t packed) { return packed & 0xffu; }
static inline uint32_t vir_known_bits_s(uint32_t packed) { return packed >> 8 & 0xffu; }

/* Computes the facts of every value of the canonical function f into out[0 .. f->nins) (f->tmp is
 * the usual place).  Block parameters start with "no value yet" and are joined over the arguments of
 * the `br`s into them; a parameter of a block with a predecessor at or after it is late, and the
 * facts that grow on a back edge jump to the next width that extensions talk about (0, 1, 8, 16, 32, 64: widening); the rounds that settle late parameters are at most ctx->budget: then every late parameter becomes
 * "nothing known" and one more round runs, which is sound.  Late parameters are found from the
 * `br`s (the only edges that carry arguments), so no analysis is asked for; the block scratch
 * vir_ctx.bs[0] is used.  Returns false when a block range is malformed. */
bool vir_known_bits_compute(vir_fn *f, const vir_pass_context *ctx, uint32_t *out);

/* The pieces of the computation, for a pass that applies facts in the same sweep when no parameter is late (one round
 * settles everything then): begin sets constants and parameters and finds the late parameters (late[] has a byte per
 * block; *has_late; loops[0..2) the blocks that hold every loop with a parameter), step does one instruction of a forward sweep. */
bool vir_known_bits_begin(vir_fn *f, uint32_t *out, uint32_t *late, bool *has_late, uint32_t *loops);
/* the rounds after begin (any: has_late), all the facts at the end */
void vir_known_bits_settle(vir_fn *f, const vir_pass_context *ctx, uint32_t *out, const uint32_t *late, const uint32_t *loops, bool any);
bool vir_known_bits_step(const vir_fn *f, uint32_t *out, const uint32_t *late, uint32_t i);

#endif
