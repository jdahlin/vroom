/* vcc code generator: AAPCS64 classification, spilling around calls */
#include "emitimpl.h"

/* ---------------- AAPCS64 (floating point and HFAs in v0-v7) ---------------- */

/*
 * A homogeneous floating-point aggregate: a record (nested records and
 * arrays included) whose leaves are 1-4 floats or 1-4 doubles, with no
 * padding.  Returns the member size (4 or 8) and sets *n, or 0.
 */
static int hfa_walk(cc *c, qty t, int *base, uint64_t *n)
{
	const ctype *y = T(c, t);

	if (y->kind == TY_FLOAT || y->kind == TY_DOUBLE || (y->kind == TY_LDOUBLE && y->size == 16)) {
		int b = (int)y->size;

		if (*base && *base != b)
			return 0;
		*base = b;
		*n = 1;
		return 1;
	}
	if (y->kind == TY_ARRAY) {
		uint64_t m;

		if (y->flags & (TF_VLA | TF_INCOMPLETE) || !hfa_walk(c, y->base, base, &m))
			return 0;
		*n = m * y->n;
		return y->n > 0;
	}
	if (y->kind == TY_STRUCT || y->kind == TY_UNION) {
		const tag *g = &c->tags[y->aux];
		uint64_t tot = 0;

		for (uint32_t k = 0; k < g->nfield; k++) {
			uint64_t m;

			if (g->f[k].bitfield || !hfa_walk(c, g->f[k].ty, base, &m))
				return 0;
			if (y->kind == TY_UNION)
				tot = m > tot ? m : tot;
			else
				tot += m;
		}
		*n = tot;
		return tot > 0 && *base && tot * (uint64_t)*base == g->size;
	}
	return 0;
}

VPRIV int hfa(cc *c, qty t, int *n)
{
	int base = 0;
	uint64_t m = 0;

	if (!is_record(c, t) || !hfa_walk(c, t, &base, &m) || m > 4)
		return 0;
	*n = (int)m;
	return base;
}

/* a record result returned through x8 */
VPRIV bool is_sret(cc *c, qty t)
{
	int n;

	return is_recabi(c, t) && tsize(c, t) > 16 && !hfa(c, t, &n);
}

/* Apple: an anonymous (variadic) argument always goes on the stack in 8-byte slots */
VPRIV void classify_va(ecx *e, qty t, int *nsaa, abiarg *a)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	uint32_t al = talign(c, t) > 8 ? 16 : 8;

	memset(a, 0, sizeof(*a));
	a->size = (uint32_t)size;
	if (is_record(c, t) && size > 16) {
		a->kind = A_REF_STACK;
		a->stk = *nsaa;
		*nsaa += 8;
		return;
	}
	*nsaa = (int)(((uint32_t)*nsaa + al - 1) & ~(al - 1));
	a->kind = A_STACK;
	a->stk = *nsaa;
	a->nreg = (uint8_t)(size > 8 ? 2 : 1);
	*nsaa += a->nreg * 8;
}

VPRIV void classify(ecx *e, qty t, int *ngrn, int *nsaa, abiarg *a)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	int n;
	uint32_t al = talign(c, t);

	memset(a, 0, sizeof(*a));
	a->size = (uint32_t)size;
	if (is_flt(c, t)) {
		if (!is_ld(c, t))
			(void)fpk(e, t);
		if (e->nsrn < 8) {
			a->kind = A_VREG;
			a->reg = (uint8_t)e->nsrn++;
			a->nreg = 1;
			return;
		}
		e->nsrn = 8;
		n = size == 16 ? 2 : 1;
		goto stack;
	}
	if (is_recabi(c, t)) {
		int hn, hb = hfa(c, t, &hn);

		if (hb) {
			if (e->nsrn + hn <= 8) {
				a->kind = A_HFA;
				a->reg = (uint8_t)e->nsrn;
				a->nreg = (uint8_t)hn;
				a->fsz = (uint8_t)hb;
				e->nsrn += hn;
				return;
			}
			e->nsrn = 8;
			n = (int)((size + 7) / 8);
			goto stack;
		}
		if (size == 0) {
			a->kind = A_IGNORE;
			return;
		}
		if (size > 16) {
			if (*ngrn < 8) {
				a->kind = A_REF_REG;
				a->reg = (uint8_t)(*ngrn)++;
				a->nreg = 1;
			} else {
				a->kind = A_REF_STACK;
				a->stk = *nsaa;
				*nsaa += 8;
			}
			return;
		}
		n = (int)((size + 7) / 8);
	} else {
		n = size == 16 ? 2 : 1;
	}
	if (n == 2 && al == 16 && (*ngrn & 1))
		(*ngrn)++;
	if (*ngrn + n <= 8) {
		a->kind = A_REG;
		a->reg = (uint8_t)*ngrn;
		a->nreg = (uint8_t)n;
		*ngrn += n;
		return;
	}
	*ngrn = 8;
stack:
	if (DARWIN(c) && !is_record(c, t)) {
		/* Apple: scalars on the stack take their natural size and alignment */
		*nsaa = (int)(((uint32_t)*nsaa + al - 1) & ~(al - 1));
		a->kind = A_STACK;
		a->stk = *nsaa;
		a->nreg = (uint8_t)n;
		a->size = (uint32_t)size;
		*nsaa += (int)size;
		return;
	}
	if (al < 8)
		al = 8;
	if (al > 16)
		al = 16;
	*nsaa = (int)(((uint32_t)*nsaa + al - 1) & ~(al - 1));
	a->kind = A_STACK;
	a->stk = *nsaa;
	a->nreg = (uint8_t)n;
	*nsaa += n * 8;
}

VPRIV void spill_live(ecx *e, spill *s)
{
	s->mask = 0;
	for (int i = 0; i < NTEMP; i++)
		if ((e->live & (1u << i)) && tempregs[i] < 19) {
			s->slot[i] = slot(e, 8, 8);
			mem(e, I_ST, 3, 0, tempregs[i], FRAME, s->slot[i]);
			s->mask |= 1u << i;
		}
}

VPRIV void unspill(ecx *e, const spill *s)
{
	for (int i = 0; i < NTEMP; i++)
		if (s->mask & (1u << i))
			mem(e, I_LD, 3, 0, tempregs[i], FRAME, s->slot[i]);
}

/* memcpy(a0, a1, n) / memset(a0, 0, n) with a0/a1 in temporaries */
VPRIV void call_fixed(ecx *e, int fn, int a0, int a1, int a2, int64_t n)
{
	spill sp;
	ins *i;

	(void)a2;
	if (!e->reach)
		return;
	e->calls = true;
	spill_live(e, &sp);
	mov(e, 1, 0, a0);
	if (a1 == -2)
		movi(e, 0, 1, 0);
	else
		mov(e, 1, 1, a1);
	movi(e, 1, 2, n);
	i = put(e, I_BL);
	i->ref = FIXED(fn);
	unspill(e, &sp);
}
