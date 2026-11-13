/*
 * A synthetic register target for testing the allocator (PLAN-vir.md V4)
 * until V2 gives the real x86-64 one: N integer and M float registers, the
 * top C and D of them callee-saved, and optionally x86-64's constraints.
 *
 *   integer registers r0..r(N-1), float f0..f(M-1) (bytes 64..)
 *   calls: integer arguments in r0..r(min(N-C-1, 6)-1) (one caller-saved
 *          register stays free for the callee's address), float arguments
 *          in f0..f(min(M-D, 8)-1), a group that does not fit goes on the
 *          stack as a whole; the SRET pointer takes the first integer
 *          register; results in r0, r1 and f0, f1; every caller-saved
 *          register is clobbered; `param` and `ret` use the same registers
 *   x86:   two-address integer and float arithmetic (the result in the
 *          first operand's register), shift counts in r2, division with the
 *          dividend and quotient in r0, the remainder in r1, r1 written
 *          before the divisor is read; switch writes r0 after reading the selector
 *          and r1 before it
 *   both:  a constant second operand of an integer operation, a constant
 *          stored value and a direct callee are immediates
 *
 * Minimum: N >= 3 with C <= N - 2, M >= 2 with D <= M - 2.
 */
#include "virimpl.h"
#include "alloc.h"
#include "synth.h"

static bool is_iconst(const vir_fn *f, uint32_t v) { return v && v < f->nconst && f->ins[v].op == VO_ICONST; }

/* the register of each value word of a signature's argument list (call order: SRET pointer,
 * then the groups), VR_NONE for the stack; *nw words */
static uint32_t assign(const vir_synth *t, const vir_fn *f, uint32_t sig, uint8_t *reg, uint32_t max,
		       uint32_t *grp0)
{
	const uint32_t *w = &f->extra[sig];
	uint32_t ng = VSIG_NGROUPS(w[0]), nw = 0, ri = 0, fi = 0;

	if (VG_KIND(w[1]) == VG_SRET) {
		if (nw < max)
			reg[nw] = ri < t->nai ? VR_MAKE(0, ri) : VR_NONE;
		ri += ri < t->nai;
		nw++;
	}
	for (uint32_t g = 0; g < ng; g++) {
		uint32_t w0 = w[3 + 2 * g], k = VG_KIND(w0), np = VG_NPIECE(w0), ni = 0, nf = 0;

		if (grp0)
			grp0[g] = nw;
		if (k == VG_MEM) {
			if (nw < max)
				reg[nw] = VR_NONE;
			nw++;
			continue;
		}
		if (k != VG_REG)
			continue;
		for (uint32_t p = 0; p < np; p++)
			VG_CLASS(w0, p) == VC_INT ? ni++ : nf++;
		bool fits = ri + ni <= t->nai && fi + nf <= t->naf;

		for (uint32_t p = 0; p < np; p++) {
			uint8_t r = VR_NONE;

			if (fits)
				r = VG_CLASS(w0, p) == VC_INT ? VR_MAKE(0, ri++) : VR_MAKE(1, fi++);
			if (nw < max)
				reg[nw] = r;
			nw++;
		}
	}
	return nw;
}

/* the register of result piece p */
static uint8_t retreg(const vir_fn *f, uint32_t sig, uint32_t p)
{
	uint32_t w0 = f->extra[sig + 1], ni = 0, nf = 0;

	if (VG_KIND(w0) != VG_REG || p >= VG_NPIECE(w0))
		return VR_NONE;
	for (uint32_t i = 0; i < p; i++)
		VG_CLASS(w0, i) == VC_INT ? ni++ : nf++;
	if (VG_CLASS(w0, p) == VC_INT)
		return ni < 2 ? VR_MAKE(0, ni) : VR_NONE;
	return nf < 2 ? VR_MAKE(1, nf) : VR_NONE;
}

static void fixed(const vir_fn *f, uint32_t v, vir_fix *x, void *ctx)
{
	const vir_synth *t = ctx;
	const vir_ins *in = &f->ins[v];
	uint8_t reg[VIR_FIX_N];

	switch (in->op) {
	case VO_PARAM: {
		uint32_t gi[256], n;

		if (in->a == VG_RESULT) {
			x->res = t->nai ? VR_MAKE(0, 0) : VR_NONE;
			return;
		}
		n = assign(t, f, f->sig, reg, VIR_FIX_N, VSIG_NGROUPS(f->extra[f->sig]) <= 256 ? gi : NULL);
		if (VSIG_NGROUPS(f->extra[f->sig]) <= 256 && in->a < 256 && VG_KIND(f->extra[f->sig + 3 + 2 * in->a]) == VG_REG &&
		    gi[in->a] + in->b < n && gi[in->a] + in->b < VIR_FIX_N)
			x->res = reg[gi[in->a] + in->b];
		return;
	}
	case VO_CALL: {
		uint32_t n = assign(t, f, in->b, reg, VIR_FIX_N - 1, NULL);

		x->nopnd = (uint8_t)(n + 1 < VIR_FIX_N ? n + 1 : VIR_FIX_N);
		for (uint32_t i = 1; i < x->nopnd; i++) {
			x->opnd[i] = reg[i - 1];
			if (reg[i - 1] == VR_NONE)
				x->mem |= 1u << i;	/* on the stack: stored from wherever it is */
		}
		x->fl |= VFX_RESTMEM;
		if (f->ins[in->a].op == VO_SYMADDR)
			x->mem |= 1;
		x->clob[0] = t->regs.alloc[0] & ~t->regs.callee[0];
		x->clob[1] = t->regs.alloc[1] & ~t->regs.callee[1];
		if (in->ty != VT_VOID && in->ty != VT_MULTI)
			x->res = retreg(f, in->b, 0);
		return;
	}
	case VO_PROJ:
		x->res = retreg(f, f->ins[in->a].b, in->b);
		return;
	case VO_RET: {
		uint32_t n = f->extra[in->c];

		x->nopnd = (uint8_t)(n < VIR_FIX_N ? n : VIR_FIX_N);
		for (uint32_t i = 0; i < x->nopnd; i++)
			x->opnd[i] = retreg(f, f->sig, i);
		return;
	}
	case VO_STORE:
		if (vir_is_const_op(f->ins[in->b].op) && f->ins[in->b].op != VO_FCONST)
			x->mem |= 2;
		return;
	case VO_SDIV: case VO_UDIV: case VO_SREM: case VO_UREM:
		if (!t->x86)
			return;
		x->nopnd = 2;
		x->opnd[0] = VR_MAKE(0, 0);
		x->res = VR_MAKE(0, in->op == VO_SDIV || in->op == VO_UDIV ? 0 : 1);
		x->eclob[0] = 2;
		x->clob[0] = 3;
		return;
	case VO_SHL: case VO_LSHR: case VO_ASHR:
		if (t->x86) {
			x->fl |= VFX_TIED;
			if (!is_iconst(f, in->b)) {
				x->nopnd = 2;
				x->opnd[1] = VR_MAKE(0, 2);
			}
		}
		if (is_iconst(f, in->b))
			x->mem |= 2;
		return;
	case VO_ADD: case VO_MUL: case VO_AND: case VO_OR: case VO_XOR:
		if (t->x86)
			x->fl |= VFX_TIED | VFX_COMMUT;
		if (is_iconst(f, in->b))
			x->mem |= 2;
		return;
	case VO_SUB:
		if (t->x86)
			x->fl |= VFX_TIED;
		if (is_iconst(f, in->b))
			x->mem |= 2;
		return;
	case VO_SWITCH:
		if (t->x86) {
			x->clob[0] = 1;       /* written after reading the selector */
			x->eclob[0] = 1u << 1;
		}
		return;
	case VO_ICMP:
		if (is_iconst(f, in->b))
			x->mem |= 2;
		return;
	case VO_NEG: case VO_NOT:
		if (t->x86)
			x->fl |= VFX_TIED;
		return;
	case VO_FADD: case VO_FMUL:
		if (t->x86)
			x->fl |= VFX_TIED | VFX_COMMUT;
		return;
	case VO_FSUB: case VO_FDIV:
		if (t->x86)
			x->fl |= VFX_TIED;
		return;
	default:
		return;
	}
}

static uint8_t opfix[256];

bool vir_synth_init(vir_synth *t, uint32_t ni, uint32_t nf, uint32_t ci, uint32_t cf, bool x86, char *err, size_t en)
{
	static const uint8_t ops[] = { VO_PARAM, VO_CALL, VO_PROJ, VO_RET, VO_STORE, VO_SDIV, VO_UDIV, VO_SREM, VO_UREM,
				       VO_SHL, VO_LSHR, VO_ASHR, VO_ADD, VO_MUL, VO_AND, VO_OR, VO_XOR, VO_SUB, VO_ICMP,
				       VO_NEG, VO_NOT, VO_FADD, VO_FMUL, VO_FSUB, VO_FDIV, VO_SWITCH };

	if (ni < 3 || ni > 64 || nf < 2 || nf > 64 || ci + 2 > ni || cf + 2 > nf) {
		snprintf(err, en, "registers: need 3 <= N <= 64 with C <= N-2, 2 <= M <= 64 with D <= M-2 (N %u M %u C %u D %u)",
			 ni, nf, ci, cf);
		return false;
	}
	memset(t, 0, sizeof(*t));
	for (size_t i = 0; i < sizeof(ops); i++)
		opfix[ops[i]] = 1;
	t->x86 = x86;
	t->regs.alloc[0] = ni == 64 ? ~0ull : (1ull << ni) - 1;
	t->regs.alloc[1] = nf == 64 ? ~0ull : (1ull << nf) - 1;
	t->regs.callee[0] = t->regs.alloc[0] & ~((1ull << (ni - ci)) - 1);
	t->regs.callee[1] = t->regs.alloc[1] & ~((1ull << (nf - cf)) - 1);
	t->nai = ni - ci - 1 < 6 ? ni - ci - 1 : 6;
	t->naf = nf - cf < 8 ? nf - cf : 8;
	t->regs.fixed = fixed;
	t->regs.ctx = t;
	t->regs.opfix = opfix;
	return true;
}
