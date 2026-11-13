/*
 * vir_known_bits_compute: the analysis behind redundant-extensions
 * (include/vir/known-bits.h, DESIGN-vir-optimizer.md 6).  One forward sweep
 * over the blocks per round; instruction facts are recomputed every round,
 * block parameter facts only grow (joined over the `br` arguments).
 */
#include "virimpl.h"
#include "vir/known-bits.h"

static inline uint32_t vir_kb_width(uint32_t ty) { return ty == VT_I32 ? 32 : ty == VT_I64 ? 64 : 0; }

/* the facts z and s, capped: z <= w, 1 <= s <= w, and s <= z + 1 when z < w */
static inline uint32_t vir_kb_pack(uint32_t z, uint32_t s, uint32_t w)
{
	if (z > w)
		z = w;
	if (s > w)
		s = w;
	if (s < 1)
		s = 1;
	if (z < w && s > z + 1)
		s = z + 1;
	return z | s << 8;
}

static inline uint32_t vir_kb_unknown(uint32_t ty)
{
	uint32_t w = vir_kb_width(ty) ? vir_kb_width(ty) : 64;

	return w | w << 8;
}

static inline uint32_t vir_kb_bitlen(uint64_t v) { return v ? 64 - (uint32_t)__builtin_clzll(v) : 0; }

/* the value of an iconst as a w-bit unsigned number */
static inline uint64_t vir_kb_iconst(const vir_ins *c, uint32_t w)
{
	uint64_t v = c->b | (uint64_t)c->c << 32;

	return w == 32 ? (uint32_t)v : v;
}

static uint32_t vir_kb_constant(const vir_ins *c)
{
	uint32_t w = vir_kb_width(c->ty), z, s;
	uint64_t v;

	if (c->op != VO_ICONST || !w)
		return vir_kb_unknown(c->ty);
	v = vir_kb_iconst(c, w);
	z = vir_kb_bitlen(v);
	if (v >> (w - 1) & 1)
		s = vir_kb_bitlen(~v & (w == 32 ? 0xffffffffull : ~0ull)) + 1;
	else
		s = z + 1;
	return vir_kb_pack(z, s, w);
}

static inline uint32_t vir_kb_join(uint32_t x, uint32_t y, uint32_t w)
{
	uint32_t z = vir_known_bits_z(x) > vir_known_bits_z(y) ? vir_known_bits_z(x) : vir_known_bits_z(y);
	uint32_t s = vir_known_bits_s(x) > vir_known_bits_s(y) ? vir_known_bits_s(x) : vir_known_bits_s(y);

	return vir_kb_pack(z, s, w);
}

/* Widening for the parameters of loops: a fact that grows jumps to the next of a few thresholds (the widths
 * extensions and masks talk about), so that a counter masked to a byte settles at z = 8 in a round or two
 * instead of climbing one bit a round until the budget runs out. */
static uint32_t vir_kb_widen(uint32_t v, bool sign, uint32_t w)
{
	static const uint8_t zt[] = { 0, 1, 8, 16, 32, 64 }, st[] = { 1, 2, 9, 17, 33, 64 };
	const uint8_t *t = sign ? st : zt;

	for (int i = 0; i < 6; i++)
		if (t[i] >= v)
			return t[i] < w ? t[i] : w;
	return w;
}

/* the shift count of instruction x if it is a constant (modulo the width), else -1 */
static int vir_kb_count(const vir_fn *f, const vir_ins *x, uint32_t w)
{
	const vir_ins *k;

	if (x->b >= f->nins)
		return -1;
	k = &f->ins[x->b];
	if (k->op != VO_ICONST)
		return -1;
	return (int)(vir_kb_iconst(k, w) & (w - 1));
}

/* the facts of the instruction at index i, from the facts of its operands in out[] */
static uint32_t vir_kb_transfer(const vir_fn *f, const uint32_t *out, uint32_t i)
{
	const vir_ins *x = &f->ins[i];
	uint32_t w = vir_kb_width(x->ty), unk = vir_kb_unknown(x->ty), za, sa, zb, sb, z, s;
	uint32_t a = x->a, b = x->b;
	int k;

	if (!w)
		return unk;
	switch (x->op) {
	case VO_LOAD: {
		uint32_t bits = 8u << VIF_W(x->fl);

		if (bits >= w)
			return unk;
		if (x->fl & VIF_MS)
			return vir_kb_pack(w, bits, w);
		return vir_kb_pack(bits, bits + 1, w);
	}
	case VO_EXT: {
		uint32_t bits = 8u << VIF_W(x->fl);

		if (a >= f->nins || bits > w)
			return unk;
		za = vir_known_bits_z(out[a]);
		sa = vir_known_bits_s(out[a]);
		if (!(x->fl & VIF_MS)) {
			z = za < bits ? za : bits;
			return vir_kb_pack(z, z + 1, w);
		}
		return vir_kb_pack(za < bits ? za : w, sa < bits ? sa : bits, w);
	}
	case VO_TRUNC:
		if (a >= f->nins)
			return unk;
		za = vir_known_bits_z(out[a]);
		sa = vir_known_bits_s(out[a]);
		return vir_kb_pack(za < 32 ? za : 32, sa <= 32 ? sa : 32, w);
	case VO_ICMP:
	case VO_FCMP:
		return vir_kb_pack(1, 2, w);
	case VO_CLZ:
	case VO_CTZ:
	case VO_POPCNT:
		return vir_kb_pack(7, 8, w);
	case VO_SELECT:
		if (b >= f->nins || x->c >= f->nins)
			return unk;
		return vir_kb_join(out[b], out[x->c], w);
	case VO_AND: case VO_OR: case VO_XOR: case VO_ADD: case VO_SUB: case VO_MUL: case VO_UDIV: case VO_UREM:
		if (a >= f->nins || b >= f->nins)
			return unk;
		za = vir_known_bits_z(out[a]), sa = vir_known_bits_s(out[a]);
		zb = vir_known_bits_z(out[b]), sb = vir_known_bits_s(out[b]);
		switch (x->op) {
		case VO_AND:
			return vir_kb_pack(za < zb ? za : zb, sa > sb ? sa : sb, w);
		case VO_OR:
		case VO_XOR:
			return vir_kb_pack(za > zb ? za : zb, sa > sb ? sa : sb, w);
		case VO_ADD:
			return vir_kb_pack((za > zb ? za : zb) + 1, (sa > sb ? sa : sb) + 1, w);
		case VO_SUB:
			return vir_kb_pack(w, (sa > sb ? sa : sb) + 1, w);
		case VO_MUL:
			return vir_kb_pack(za + zb, w, w);
		case VO_UDIV:
			return vir_kb_pack(za, za + 1, w);
		default:	/* urem */
			z = za < zb ? za : zb;
			return vir_kb_pack(z, z + 1, w);
		}
	case VO_SHL: case VO_LSHR: case VO_ASHR:
		if (a >= f->nins || (k = vir_kb_count(f, x, w)) < 0)
			return unk;
		za = vir_known_bits_z(out[a]);
		sa = vir_known_bits_s(out[a]);
		if (x->op == VO_SHL)
			return vir_kb_pack(za + (uint32_t)k, sa + (uint32_t)k, w);
		if (x->op == VO_LSHR) {
			z = za > (uint32_t)k ? za - (uint32_t)k : 0;
			return vir_kb_pack(z, z + 1, w);
		}
		return vir_kb_pack(za < w ? (za > (uint32_t)k ? za - (uint32_t)k : 0) : w, sa > (uint32_t)k + 1 ? sa - (uint32_t)k : 1, w);
	default:
		return unk;
	}
}

/* The fact of instruction i is written to out[i]; a `br` joins its arguments into the parameters of its target.
 * True when a late parameter's facts grew.  A value of another type than i32 and i64 gets no fact (nothing reads it). */
bool vir_known_bits_step(const vir_fn *f, uint32_t *out, const uint32_t *late, uint32_t i)
{
	const vir_ins *x = &f->ins[i];

	if (x->op == VO_BPARAM)
		return false;	/* accumulated by the brs into it */
	if (x->op == VO_BR) {
		const vir_blk *t = &f->blk[x->a];
		bool grew = false;

		for (uint32_t j = 0; j < t->nparam; j++) {
			uint32_t arg = f->extra[x->c + j], p = t->first + j, w = vir_kb_width(f->ins[p].ty), nv;

			if (!w || arg >= f->nins)
				continue;
			bool back = late[x->a] && i >= f->blk[x->a].first;	/* from the loop's own blocks: the edge that decides */

			nv = vir_kb_join(out[p], out[arg], w);
			if (nv != out[p] && back)
				nv = vir_kb_pack(vir_kb_widen(vir_known_bits_z(nv), false, w), vir_kb_widen(vir_known_bits_s(nv), true, w), w);
			if (nv != out[p]) {
				out[p] = nv;
				grew |= back;
			}
		}
		return grew;
	}
	if (x->ty == VT_I32 || x->ty == VT_I64)
		out[i] = vir_kb_transfer(f, out, i);
	return false;
}

/* one forward sweep over the blocks [from, to); true when a late block parameter's facts grew on a back edge */
static bool vir_kb_sweep(vir_fn *f, uint32_t *out, const uint32_t *late, uint32_t from, uint32_t to)
{
	bool grew = false;

	for (uint32_t bi = from; bi < to; bi++) {
		const vir_blk *kb = &f->blk[bi];

		for (uint32_t i = kb->first; i < kb->first + kb->n; i++)
			grew |= vir_known_bits_step(f, out, late, i);
	}
	return grew;
}

bool vir_known_bits_begin(vir_fn *f, uint32_t *out, uint32_t *late, bool *has_late, uint32_t *loops)
{
	bool any = false;

	if (!f->nblk || f->nconst > f->nins)
		return false;
	out[0] = vir_kb_unknown(VT_VOID);
	for (uint32_t i = 1; i < f->nconst; i++)
		out[i] = vir_kb_constant(&f->ins[i]);
	loops[0] = f->nblk;
	loops[1] = 0;
	/* only a `br` passes arguments to parameters, so a parameter is late when a `br` at or after its block targets it (no
	   predecessor lists needed); the instructions are all written by the sweeps, the parameters start with "no value yet" */
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *kb = &f->blk[b];
		const vir_ins *t;

		late[b] = 0;
		if (!kb->n || kb->first + kb->n > f->nins || kb->nparam > kb->n)
			return false;
		t = &f->ins[kb->first + kb->n - 1];
		if (t->op == VO_BR && t->a < f->nblk && t->a <= b && f->blk[t->a].nparam) {
			late[t->a] = 1;
			any = true;
			if (t->a < loops[0])
				loops[0] = t->a;
			loops[1] = b + 1;	/* the blocks [loops[0], loops[1]) hold every loop that carries a parameter */
		}
		for (uint32_t i = kb->first; i < kb->first + kb->nparam; i++)
			out[i] = vir_kb_width(f->ins[i].ty) ? vir_kb_pack(0, 1, vir_kb_width(f->ins[i].ty)) : vir_kb_unknown(f->ins[i].ty);
	}
	*has_late = any;
	return true;
}

bool vir_known_bits_compute(vir_fn *f, const vir_pass_context *ctx, uint32_t *out)
{
	uint32_t *late = vir_ctx_get()->bs[0], loops[2];
	bool any;

	if (!vir_known_bits_begin(f, out, late, &any, loops))
		return false;
	vir_known_bits_settle(f, ctx, out, late, loops, any);
	return true;
}

void vir_known_bits_settle(vir_fn *f, const vir_pass_context *ctx, uint32_t *out, const uint32_t *late, const uint32_t *loops, bool any)
{
	uint32_t budget = ctx && ctx->budget ? ctx->budget : VIR_PASS_BUDGET, round = 1;
	bool grew;

	/* round 1 is the whole function; while a back edge keeps growing a parameter only the blocks of the loops are swept
	   again, and the blocks after them once the parameters have settled */
	grew = vir_kb_sweep(f, out, late, 0, f->nblk) && any;
	while (grew && round < budget) {
		round++;
		grew = vir_kb_sweep(f, out, late, loops[0], loops[1]);
	}
	if (grew) {	/* out of budget: the late parameters know nothing, and one round settles the rest */
		for (uint32_t b = 0; b < f->nblk; b++)
			if (late[b])
				for (uint32_t j = 0; j < f->blk[b].nparam; j++)
					out[f->blk[b].first + j] = vir_kb_unknown(f->ins[f->blk[b].first + j].ty);
		vir_kb_sweep(f, out, late, loops[0], f->nblk);
	} else if (round > 1) {
		vir_kb_sweep(f, out, late, loops[1], f->nblk);
	}
}
