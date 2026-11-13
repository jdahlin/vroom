/* vcc code generator: temporaries, frame slots, values, lvalues */
#include "../../../codegen/emitimpl.h"

/* ---------------- temporaries and frame slots ---------------- */

VPRIV int treg(ecx *e)
{
	for (int i = 0; i < NTEMP; i++)
		if (!((e->live | e->resv) & (1u << i))) {
			e->live |= 1u << i;
			if (tempregs[i] >= 19)
				e->usedcs |= 1u << tempregs[i];
			return tempregs[i];
		}
	if (e->nlr || (e->inlfree && e->inlfree < INLINE_FREE_SAFE) || !e->spill)
		_longjmp(e->bail, 2);	/* emit_function tries again more carefully */
	{
		const fnctx *f = e->f;

		while (f->up)
			f = f->up;
		if (getenv("VCC_LEAKCHK"))
			ediag(e, e->curloc, "here");
		efatal(e, e->c->syms[f->fn].loc, "expression too complex (out of registers) in '%.*s'",
		       (int)vintern_ents[e->c->syms[f->fn].name].len, vintern_ents[e->c->syms[f->fn].name].s);
	}
}


VPRIV void tfree(ecx *e, int r)
{
	int i = tidx(r);

	if (i >= 0)
		e->live &= ~(1u << i);
}

VPRIV void val_free(ecx *e, const val *v)
{
	if (v->k == V_REG)
		tfree(e, v->r);
	else if (v->k == V_REG2)
		tfree(e, v->r), tfree(e, v->r2);
}

/* a stack slot: returns its offset (FRAME base) */
VPRIV int64_t slot(ecx *e, uint64_t size, uint32_t align)
{
	if (align < 1)
		align = 1;
	if (align > 16) {	/* x28: the base of the realigned frame if it turns out dynamic */
		if (e->nlr)
			_longjmp(e->bail, 2);	/* it holds a register variable (they start at x28): again without */
		e->resv |= 1u << tidx(R_BP);
	}
	if (align > e->falign)
		e->falign = align;	/* over 16: finish_function realigns sp to it */
	/* Keep the allocation and its alignment rounding within signed offsets. */
	uint64_t limit = (uint64_t)INT64_MAX - align;
	if ((uint64_t)e->foff > limit || size > limit - (uint64_t)e->foff)
		efatal(e, 0, "stack object too large");
	/* allocations past the current top ended with their scope */
	while (e->nsstk && e->sla[e->sstk[e->nsstk - 1]].off < -e->foff)
		e->nsstk--;
	e->foff = (int64_t)(((uint64_t)e->foff + size + align - 1) & ~(uint64_t)(align - 1));
	if (e->foff > e->fmax)
		e->fmax = e->foff;
	if (e->nsla < CAPSLA) {
		e->sla[e->nsla] = (slotal){ -e->foff, size };
		e->sstk[e->nsstk++] = e->nsla;
	}
	e->nsla++;
	return -e->foff;
}

/* tag record i (frame offset off, len bytes; -1: an address) with its live allocation */
VPRIV void slot_tag(ecx *e, ins *i, int64_t off, int64_t len)
{
	uint32_t lo = 0, hi = e->nsstk;

	if (e->E->o->opt < 1 || e->novtag)
		return;
	/* live allocations are disjoint, offsets decreasing with depth */
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (e->sla[e->sstk[mid]].off > off)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < e->nsstk) {
		const slotal *a = &e->sla[e->sstk[lo]];
		uint32_t id = e->sstk[lo] + 1;

		if (off >= a->off && (len < 0 ? off <= a->off + (int64_t)a->size : off + len <= a->off + (int64_t)a->size) &&
		    id <= 0xffff)
			i->rm = (uint8_t)id, i->ra = (uint8_t)(id >> 8);
		/* an address at the start of one allocation is also just past the end of the one below */
		if (len < 0 && off == a->off && lo + 1 < e->nsstk) {
			const slotal *b = &e->sla[e->sstk[lo + 1]];
			uint32_t id2 = e->sstk[lo + 1] + 1;

			if (b->off + (int64_t)b->size == off) {
				if (id2 > 0xffff)
					i->rm = i->ra = 0;	/* untagged */
				else
					i->cond = (uint8_t)id2, i->sh = (uint8_t)(id2 >> 8);
			}
		}
	}
}
VPRIV bool sym_needs_got(cc *c, uint32_t ref);

/* address of a symbol into rd */
VPRIV void addr_of_ref(ecx *e, int rd, uint32_t ref, int64_t off)
{
	cc *c = e->c;

	note_ref(e, ref);
	if (sym_needs_got(c, ref)) {
		symop(e, I_LDGOT, 1, 0, rd, 0, ref, 0);
		if (off)
			opi(e, I_ADDI, 1, rd, rd, off);
		return;
	}
	if (!SYMOFF_OK(c, off)) {
		symop(e, I_ADRP, 1, 0, rd, 0, ref, 0);
		symop(e, I_ADDLO, 1, 0, rd, rd, ref, 0);
		opi(e, I_ADDI, 1, rd, rd, (int64_t)off);
		return;
	}
	symop(e, I_ADRP, 1, 0, rd, 0, ref, off);
	symop(e, I_ADDLO, 1, 0, rd, rd, ref, off);
}

/* value into a (new) register; constants are materialised */
VPRIV int toreg(ecx *e, val *v)
{
	int r;

	switch (v->k) {
	case V_REG:
		return v->r;
	case V_REG2:
		tfree(e, v->r2);
		*v = vreg(v->r);
		return v->r;
	case V_CONST:
		r = treg(e);
		movi(e, 1, r, (int64_t)v->i);
		*v = vreg(r);
		return r;
	case V_ADDR:
		r = treg(e);
		addr_of_ref(e, r, v->ref, (int64_t)v->i);
		*v = vreg(r);
		return r;
	default:
		r = treg(e);
		movi(e, 1, r, 0);
		*v = vreg(r);
		return r;
	}
}

/* 128-bit value into a register pair */
VPRIV void toreg2(ecx *e, val *v, bool sgn)
{
	if (v->k == V_REG2)
		return;
	if (v->k == V_CONST) {
		int lo = treg(e), hi = treg(e);

		movi(e, 1, lo, (int64_t)(uint64_t)(u128)v->i);
		movi(e, 1, hi, (int64_t)(uint64_t)((u128)v->i >> 64));
		v->k = V_REG2, v->r = (uint8_t)lo, v->r2 = (uint8_t)hi;
		return;
	}
	{
		int lo = toreg(e, v), hi = treg(e);

		if (sgn)
			opbf(e, I_SBFM, 1, hi, lo, 63, 63);
		else
			movi(e, 1, hi, 0);
		v->k = V_REG2, v->r = (uint8_t)lo, v->r2 = (uint8_t)hi;
	}
}

VPRIV void lfree(ecx *e, const lval *l)
{
	if (l->k == L_REG)
		tfree(e, l->r);
}

/* base register + offset for a memory access; may load a symbol page into x16 */
VPRIV int lbase(ecx *e, const lval *l, int64_t *off, int size)
{
	switch (l->k) {
	case L_FRAME:
		*off = l->off;
		return FRAME;
	case L_REG:
		*off = l->off;
		return l->r;
	case L_VREG:
		efatal(e, e->curloc, "internal: memory access to a register variable");
	default:
		addr_of_ref(e, R_X16, l->ref, l->off);
		*off = 0;
		(void)size;
		return R_X16;
	}
}

VPRIV int log2sz(uint64_t n) { return n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : 3; }

VPRIV bool sym_needs_got(cc *c, uint32_t ref)
{
	if (RKIND(ref) == RK_NAME)
		return DARWIN(c);
	if (RKIND(ref) != RK_SYM)
		return false;
	{
		const sym *y = &c->syms[canon_sym(c, RIDX(ref))];

		if (DARWIN(c) && y->name && y->sc != SC_STATIC && (y->kind == S_FUNC || y->kind == S_VAR) &&
		    (!(y->flags & (SF_DEFINED | SF_TENTATIVE)) || (y->flags & SF_WEAK) || (y->kind == S_FUNC && !y->body)))
			return true;	/* may live in a dylib: ld64 relaxes the GOT load when it does not */
		if (!DARWIN(c) && c->o->pic && y->name && y->sc != SC_STATIC && (y->kind == S_FUNC || y->kind == S_VAR)) {
			/* clang: -fPIC: every preemptible symbol; -fPIE: those not defined here.
			 * Hidden/protected ones are direct. */
			const attrs *a = sym_attrs(c, canon_sym(c, RIDX(ref)));

			if (a && a->visibility) {
				const char *v = c->strs[a->visibility - 1].s;

				if (!strcmp(v, "hidden") || !strcmp(v, "protected"))
					return false;
			}
			if (c->o->pic == 2)
				return true;
			if (a && a->alias)
				return false;	/* an alias or ifunc is defined here */
			if (!(y->flags & (SF_DEFINED | SF_TENTATIVE)) || (y->kind == S_FUNC && !y->body))
				return true;
		}
		return (y->flags & SF_WEAK) && !(y->flags & SF_DEFINED);
	}
}

/* the alignment the object a reference names is known to have */
static uint32_t ref_align(ecx *e, const lval *l)
{
	cc *c = e->c;
	uint32_t a;

	switch (RKIND(l->ref)) {
	case RK_SYM: a = sym_align(c, canon_sym(c, RIDX(l->ref))); break;
	case RK_STR: a = c->strs[RIDX(l->ref)].esz; break;
	case RK_LBL: a = 1; break;
	default: return l->align;
	}
	return a < l->align ? a : l->align;
}

/* load a scalar of size n (1,2,4,8) into rd; ext: 0 zero, 1 sign to x */
VPRIV void load_n(ecx *e, int rd, const lval *l, int n, int ext)
{
	int64_t off;
	int sz = log2sz((uint64_t)n);

	if (l->k == L_SYM && !sym_needs_got(e->c, l->ref) && ref_align(e, l) >= (uint32_t)n && (l->off & (n - 1)) == 0 &&
	    SYMOFF_OK(e->c, l->off)) {
		note_ref(e, l->ref);
		symop(e, I_ADRP, 1, 0, R_X16, 0, l->ref, l->off);
		symop(e, I_LDLO, sz, ext, rd, R_X16, l->ref, l->off);
		return;
	}
	{
		int b = lbase(e, l, &off, n);

		mem(e, I_LD, sz, ext, rd, b, off);
	}
}

VPRIV void store_n(ecx *e, int rs, const lval *l, int64_t extra, int n)
{
	int64_t off;
	int sz = log2sz((uint64_t)n);

	if (l->k == L_SYM && !sym_needs_got(e->c, l->ref) && ref_align(e, l) >= (uint32_t)n && ((l->off + extra) & (n - 1)) == 0 &&
	    SYMOFF_OK(e->c, l->off + extra)) {
		note_ref(e, l->ref);
		symop(e, I_ADRP, 1, 0, R_X16, 0, l->ref, l->off + extra);
		symop(e, I_STLO, sz, 0, rs, R_X16, l->ref, l->off + extra);
		return;
	}
	{
		int b = lbase(e, l, &off, n);

		mem(e, I_ST, sz, 0, rs, b, off + extra);
	}
}

/* address of an lvalue into a new register (or a constant address) */
VPRIV val laddr(ecx *e, lval *l)
{
	int r;

	switch (l->k) {
	case L_SYM:
		if (unlikely(ref_weak(e->c, l->ref))) {
			r = treg(e);
			addr_of_ref(e, r, l->ref, l->off);
			return vreg(r);
		}
		return vaddr(l->ref, l->off);
	case L_FRAME:
		r = treg(e);
		opi(e, I_ADDI, 1, r, FRAME, l->off);
		return vreg(r);
	case L_REG:
		if (l->off)
			opi(e, I_ADDI, 1, l->r, l->r, l->off);
		l->off = 0;
		return vreg(l->r);
	case L_VREG:
		efatal(e, e->curloc, "internal: address of a register variable");
	default:
		r = treg(e);
		mov(e, 1, r, l->r);
		return vreg(r);
	}
}

/* lvalue from an address value */
VPRIV lval lfromval(ecx *e, val v, uint32_t align)
{
	lval l = { 0 };

	l.align = align;
	if (v.k == V_ADDR) {
		l.k = L_SYM;
		l.ref = v.ref;
		l.off = (int64_t)v.i;
		return l;
	}
	l.k = L_REG;
	l.r = (uint8_t)toreg(e, &v);
	return l;
}

VPRIV void copy_mem(ecx *e, int dst, int src, uint64_t n)
{
	if (n <= 128) {
		uint64_t o = 0;
		int t = R_X16;	/* not x17: the backend uses it for large offsets */

		while (o < n) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			mem(e, I_LD, log2sz(k), 0, t, src, (int64_t)o);
			mem(e, I_ST, log2sz(k), 0, t, dst, (int64_t)o);
			o += k;
		}
		return;
	}
	call_fixed(e, FN_MEMCPY, dst, src, -1, (int64_t)n);
}

VPRIV void zero_mem(ecx *e, int dst, uint64_t n)
{
	if (n <= 256) {
		uint64_t o = 0;

		while (o < n) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			mem(e, I_ST, log2sz(k), 0, ZR, dst, (int64_t)o);
			o += k;
		}
		return;
	}
	call_fixed(e, FN_MEMSET, dst, -2, -1, (int64_t)n);
}
