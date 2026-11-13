/*
 * redundant-extensions (DESIGN-vir-optimizer.md 6): an `ext` of a value that
 * already fits, and an `and` with a low mask of a value that already fits it,
 * is that value.  Facts come from vir_known_bits_compute; the rewrite is one
 * forward sweep with a replacement map in vir_ctx.aux (written when a value
 * is visited, so nothing is cleared), so chains collapse in one run:
 *
 *   R1  ext.Wu with the result type of its operand and z(a) <= 8*2^W: a
 *   R2  ext.Ws with the result type of its operand and s(a) <= 8*2^W, or z(a) < 8*2^W: a
 *   R3  and a, k with k a low mask 2^m - 1 (m >= z(a), or all ones): a
 *
 * R4 of the design (ext.1u/ext.2u i32 -> i64 rewritten to the plain widening)
 * is left out: it only pays off with the lowering rule of the design's Q5.
 *
 * Pure operations only; no block, edge or terminator changes (keeps CFG and
 * dominators); no target fact.  Replaced instructions become nops and
 * vir_function_compact drops them and the constants only they used.
 */
#include "../virimpl.h"
#include "vir/known-bits.h"
#include "vir/pass.h"

/* the low-mask constant value of an `and` operand: its mask bits (k = 2^m - 1, m >= 1) or 0 */
static uint64_t vir_redundant_mask(const vir_fn *f, uint32_t v, uint32_t w)
{
	const vir_ins *c;
	uint64_t k;

	if (v >= f->nconst || v == 0)
		return 0;
	c = &f->ins[v];
	if (c->op != VO_ICONST)
		return 0;
	k = c->b | (uint64_t)c->c << 32;
	if (w == 32)
		k = (uint32_t)k;
	return k && !(k & (k + 1)) ? k : 0;	/* a run of ones from bit 0 (k + 1 wraps to 0 for all ones) */
}

static inline bool vir_redundant_integer(uint32_t ty) { return ty == VT_I32 || ty == VT_I64; }

static bool vir_redundant_candidate(const vir_fn *f, const vir_ins *x)
{
	if (x->op == VO_EXT)
		return true;
	if (x->op == VO_AND && vir_redundant_integer(x->ty)) {
		uint32_t w = x->ty == VT_I32 ? 32 : 64;

		return vir_redundant_mask(f, x->b, w) || vir_redundant_mask(f, x->a, w);
	}
	return false;
}

/* Instruction i (its operands already replaced): the value that is equal to it and defined before, or i itself */
static uint32_t vir_redundant_replacement(const vir_fn *f, const uint32_t *kb, uint32_t i)
{
	const vir_ins *in = &f->ins[i];
	uint32_t w = in->ty == VT_I32 ? 32 : 64;

	if (in->op == VO_EXT) {
		uint32_t bits = 8u << VIF_W(in->fl), z, s;

		if (in->a >= i || !vir_redundant_integer(in->ty) || f->ins[in->a].ty != in->ty)
			return i;
		z = vir_known_bits_z(kb[in->a]);
		s = vir_known_bits_s(kb[in->a]);
		return (!(in->fl & VIF_MS) ? z <= bits : s <= bits || z < bits) ? in->a : i;
	}
	if (in->op == VO_AND && vir_redundant_integer(in->ty)) {
		uint64_t m = vir_redundant_mask(f, in->b, w);
		uint32_t other = in->a, z;

		if (!m) {
			m = vir_redundant_mask(f, in->a, w);
			other = in->b;
		}
		if (!m || other >= i || f->ins[other].ty != in->ty)
			return i;
		z = vir_known_bits_z(kb[other]);
		if (z < w ? (m & (((uint64_t)1 << z) - 1)) == ((uint64_t)1 << z) - 1 : m == (w == 32 ? 0xffffffffull : ~0ull))
			return other;
	}
	return i;
}

/* the operands of instruction i through the replacement map (constants are their own) */
static inline void vir_redundant_operands(vir_fn *f, const uint32_t *rep, uint32_t i)
{
	vir_ins *in = &f->ins[i];
	uint8_t m = vir_value_fields(in->op);

	if (m & 1 && in->a >= f->nconst && in->a < i)
		in->a = rep[in->a];
	if (m & 2 && in->b >= f->nconst && in->b < i)
		in->b = rep[in->b];
	if (m & 4 && in->c >= f->nconst && in->c < i)
		in->c = rep[in->c];
	if (m & VIR_FIELDS_LIST) {
		vir_oit it;
		uint32_t *p;

		vir_oit_init_list(&it, f, in);
		while ((p = vir_oit_next(&it)))
			if (*p >= f->nconst && *p < i)
				*p = rep[*p];
	}
}

bool vir_redundant_extensions_remove(vir_fn *f, vir_pass_context *ctx)
{
	vir_ctx *x = vir_ctx_get();
	uint32_t *rep = x->aux, *kb = f->tmp, *late = x->bs[0], loops[2], removed = 0;
	bool any = false, has_late;

	for (uint32_t b = 0; b < f->nblk && !any; b++)
		for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n && !any; i++)
			any = vir_redundant_candidate(f, &f->ins[i]);
	if (!any || !vir_known_bits_begin(f, kb, late, &has_late, loops))
		return false;
	/* with a late parameter the facts need rounds before any of them may be used: compute them all, then sweep */
	if (has_late)
		vir_known_bits_settle(f, ctx, kb, late, loops, true);
	/* one forward sweep: replacement map written as values are visited (nothing is cleared); facts are computed in the
	   same sweep when no parameter is late; a replaced value's facts are never read (its uses name the replacement) */
	for (uint32_t b = 0; b < f->nblk; b++)
		for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n; i++) {
			uint32_t r;

			if (removed)	/* (until the first removal every operand is its own replacement) */
				vir_redundant_operands(f, rep, i);
			r = f->ins[i].op == VO_EXT || f->ins[i].op == VO_AND ? vir_redundant_replacement(f, kb, i) : i;
			if (r != i) {
				rep[i] = r;
				memset(&f->ins[i], 0, sizeof f->ins[i]);	/* a nop: pure, and every use comes later and is rewritten */
				removed++;
				continue;
			}
			rep[i] = i;
			if (!has_late)
				vir_known_bits_step(f, kb, late, i);
		}
	if (!removed)
		return false;
	ctx->changes += removed;
	vir_function_compact(f);
	return true;
}
