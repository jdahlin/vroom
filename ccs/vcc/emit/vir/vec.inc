/*
 * The loop vectorizer (RESEARCH-vectors.md 6; SPEC-vir.md 7.5), #included by build.c: it works
 * on the pre-IR, after if-conversion and before the edges are split, where blocks and
 * instructions can still be added.
 *
 * A loop it takes: innermost; the header H has two predecessors (P outside, the latch) and ends
 * in `brif icmp(u, n)` with u linear in the iteration and n invariant; the body is a chain of
 * blocks with one way in and one way out.  Every parameter of H is an induction (p + K each
 * iteration) or an i32 sum.  The instructions are pure, loads or stores; a load or store of 4
 * bytes addresses `symbol + 4*t + ...` or `symbol - 4*t + ...`, or (a load) an address that does
 * not change and that no store of the loop can reach.
 *
 * What it builds, in front of the loop, which stays as it is and runs what is left:
 *
 *   P:    ...  br VPRE
 *   VPRE: d = the iterations' span (n - u at entry), in 64 bits
 *         brif d >= 4*|step|, VPH, J
 *   VPH:  nv = trip count & ~3; the invariants' splats
 *   VB(cnt, the inductions, the sums' lanes):
 *         the body on four lanes; cnt -= 4; brif cnt != 0, VB, VX
 *   VX:   the sums folded;  br J
 *   J(the values of H's parameters after 0 or nv iterations):  br H
 *
 * The vector body executes the loads and stores of whole iterations of the scalar loop, in the
 * scalar loop's order within an iteration, so it reads and writes nothing the program would not
 * (4.1, 4.2).  Two accesses of one symbol at a distance of 1-3 iterations are allowed only when
 * the earlier iteration's one is not a store read back, and comes first in the body (6.3).
 * Nothing is reassociated: there are no float reductions (4.5).
 *
 * A value of the loop is one of: invariant (INV), linear in the iteration (LIN: an induction and
 * what add, sub, mul by a constant and ext make of it), different in each lane (VEC), a
 * comparison (MASK), a sum's parameter (RED) or its update (REDUPD).  `exact` says the linear
 * form holds as integers (an i32 operation without nsw may wrap): addresses and the trip count
 * need it, lanes do not.
 */

enum { VK_NONE, VK_INV, VK_LIN, VK_VEC, VK_MASK, VK_RED, VK_REDUPD, VK_BAD };
enum { VJ_NONE, VJ_SYM, VJ_PTR };

typedef struct vvi {
	uint8_t kind, exact, objk;
	uint8_t uns;		/* an i32 whose form is its unsigned value (zero-extends; a signed operation on it is not exact) */
	uint16_t nuse;		/* uses by the loop's instructions and terminators */
	int64_t c;		/* what one iteration adds */
	uint32_t objv, bv;	/* the object addressed (a symbol ref, a pointer value); an invariant term */
	int64_t bs, k;		/* ... its factor; the constant term */
	uint32_t sce, scb, sc0, vec;	/* its value at entry (in VPRE), in the vector body or VPH, at t = 0 (in VPH), its lanes */
} vvi;

typedef struct vvacc { uint32_t q, pos; bool store; } vvacc;
typedef struct vvred { uint32_t p, upd, x, acc, accnext; } vvred;

typedef struct vvs {
	ecx *e;
	vvi *I; uint32_t n0;			/* per value below n0 */
	uint32_t *touch; uint32_t ntouch, ctouch;
	uint8_t *inl;				/* per block: in the loop */
	uint32_t lb[64], nlb;			/* the loop's blocks, H first */
	uint32_t H, P, L, X, jP, jL;		/* header, entry predecessor, latch, exit; their slots in H's predecessors */
	uint32_t VPRE, VPH, VB, VX, J;
	vvacc acc[64]; uint32_t nacc;
	uint32_t invld[64], ninvld;		/* loads taken as invariant */
	vvred red[8]; uint32_t nred;
	uint32_t ind[32], indb[32], indn[32], nind;	/* H's inductions; VB's parameter and its next value */
	int64_t indk[32];			/* ... what an iteration adds */
	uint32_t one[8], none;			/* steps that are values: the vector loop runs when each is 1 */
	uint32_t stepv[4]; int64_t stepc[4]; uint32_t nstep;	/* lanes 0, c, 2c, 3c (in VPH) */
	struct { uint32_t a, b; } chk[8]; uint32_t nchk;	/* pairs whose ranges must not meet (run time): an access's index, or 64 + an invariant load's */
	struct { uint32_t a, par; bool rev; } pin[32]; uint32_t npin;	/* per address: VB's pointer to its four elements */
	uint32_t pos;
	const char *why;			/* why not (NULL: so far, yes) */
} vvs;

static bool vv_no(vvs *S, const char *why)
{
	if (!S->why)
		S->why = why;
	return false;
}

static vvi *vv_touch(vvs *S, uint32_t v)
{
	ecx *e = S->e;

	GROW(S->touch, S->ntouch, S->ctouch, 1);
	S->touch[S->ntouch++] = v;
	return &S->I[v];
}

/* v's record; a value from outside the loop gets one on first sight */
static vvi *vv_get(vvs *S, uint32_t v)
{
	vwst *V = S->e->vw;
	static _Thread_local vvi bad;
	vvi *x;
	const vwp *q;

	if (v >= S->n0) {
		bad = (vvi){ .kind = VK_BAD };
		return &bad;
	}
	x = &S->I[v];
	if (x->kind)
		return x;
	q = &V->p[v];
	if (q->blk != VW_NOBLK && S->inl[q->blk]) {	/* (used before it was classified: not in the chain's order) */
		bad = (vvi){ .kind = VK_BAD };
		return &bad;
	}
	vv_touch(S, v);
	*x = (vvi){ .kind = VK_INV, .exact = 1 };
	if (q->blk == VW_NOBLK && q->in.op == VO_ICONST) {
		x->k = q->in.ty == VT_I32 ? (int64_t)(int32_t)q->in.b : (int64_t)((uint64_t)q->in.b | (uint64_t)q->in.c << 32);
	} else if (q->blk == VW_NOBLK && q->in.op == VO_SYMADDR) {
		x->objk = VJ_SYM, x->objv = q->in.a;
		x->k = (int64_t)((uint64_t)q->in.b | (uint64_t)q->in.c << 32);
	} else if (q->in.ty == VT_PTR) {
		x->objk = VJ_PTR, x->objv = v;
	} else {
		x->bv = v, x->bs = 1;
	}
	return x;
}

static bool vv_isk(const vvi *x) { return x->kind == VK_INV && x->exact && !x->bv && !x->objk; }	/* a constant */
static bool vv_lin(const vvi *x) { return x->kind == VK_INV || x->kind == VK_LIN; }
static bool vv_lane(const vvi *x) { return x->kind == VK_INV || x->kind == VK_LIN || x->kind == VK_VEC; }

/* r = a + s*b as linear forms; false when it has no such form (two objects, two invariant terms) */
static bool vv_comb(vvi *r, const vvi *a, const vvi *b, int s)
{
	r->c = (int64_t)((uint64_t)a->c + (uint64_t)s * (uint64_t)b->c);
	r->k = (int64_t)((uint64_t)a->k + (uint64_t)s * (uint64_t)b->k);
	if (b->objk && (a->objk || s < 0))
		return false;
	r->objk = a->objk ? a->objk : b->objk, r->objv = a->objk ? a->objv : b->objv;
	if (!a->bv) {
		r->bv = b->bv, r->bs = s * b->bs;
	} else if (!b->bv) {
		r->bv = a->bv, r->bs = a->bs;
	} else if (a->bv == b->bv) {
		r->bv = a->bv, r->bs = a->bs + s * b->bs;
		if (!r->bs)
			r->bv = 0;
	} else {
		return false;
	}
	return true;
}

/* q is an invariant with no form of its own: itself as the term (a pointer keeps its object) */
static void vv_opaque(vvs *S, uint32_t q, vvi *r)
{
	uint8_t ok = r->objk;
	uint32_t ov = r->objv;

	*r = (vvi){ .kind = VK_INV, .exact = 1, .nuse = r->nuse, .bv = q, .bs = 1 };
	if (S->e->vw->p[q].in.ty == VT_PTR) {
		r->objk = ok ? ok : VJ_PTR, r->objv = ok ? ov : q;
		if (!ok)
			r->bv = 0, r->bs = 0;
	}
}

static bool vv_skip(vwst *V, uint32_t q)
{
	return vw_find(V, q) != q || vw_promdead(V, q) || (V->p[q].in.op == VO_MEMSET && (V->p[q].in.fl & VW_LIFETIME));
}

/* instruction q of the loop: its kind.  false: the loop is not vectorized */
static bool vv_classify(vvs *S, uint32_t q)
{
	vwst *V = S->e->vw;
	vir_ins in = V->p[q].in;
	const vir_opinfo *oi = &vir_optab[in.op];
	uint32_t w[3] = { in.a, in.b, in.c };
	vvi *o[3] = { NULL, NULL, NULL }, r = { 0 };
	bool allinv = true;
	int no = 0;

	for (int k = 0; k < 3; k++)
		if (vw_isval(oi->k[k])) {
			w[k] = vw_find(V, w[k]);
			o[k] = vv_get(S, w[k]);
			if (w[k] < S->n0 && o[k]->nuse < 0xffff)
				o[k]->nuse++;
			allinv &= o[k]->kind == VK_INV;
			no++;
		}
	for (uint32_t i = 0; i < S->nred; i++)
		if (S->red[i].upd == q) {
			vvi *x = vv_get(S, vw_find(V, S->red[i].x));

			if (!vv_lane(x))
				return vv_no(S, "a sum of what is not a lane");
			r.kind = VK_REDUPD;
			goto done;
		}
	switch (in.op) {
	case VO_ADD: case VO_SUB: case VO_PTRADD:
		if (vv_lin(o[0]) && vv_lin(o[1])) {
			bool rep = vv_comb(&r, o[0], o[1], in.op == VO_SUB ? -1 : 1);
			bool ex = rep && o[0]->exact && o[1]->exact && (in.ty != VT_I32 || ((in.fl & VIF_NSW) && !o[0]->uns && !o[1]->uns));

			r.kind = allinv ? VK_INV : VK_LIN, r.exact = ex;
			if (allinv && !ex)
				vv_opaque(S, q, &r);
			else if (!ex && in.ty != VT_I32)
				r.kind = VK_BAD;
		} else {
			r.kind = in.ty == VT_I32 && vv_lane(o[0]) && vv_lane(o[1]) ? VK_VEC : VK_BAD;
		}
		break;
	case VO_MUL: case VO_SHL: {
		int kc = vv_isk(o[1]) ? 1 : in.op == VO_MUL && vv_isk(o[0]) ? 0 : -1;

		if (kc >= 0 && vv_lin(o[1 - kc]) && !o[1 - kc]->objk && (in.op == VO_MUL || (in.ty == VT_I64 && o[1]->k >= 0 && o[1]->k < 63))) {
			const vvi *x = o[1 - kc];
			uint64_t K = in.op == VO_MUL ? (uint64_t)o[kc]->k : 1ull << o[1]->k;

			r = (vvi){ .kind = allinv ? VK_INV : VK_LIN, .c = (int64_t)((uint64_t)x->c * K), .bv = x->bv,
				   .bs = (int64_t)((uint64_t)x->bs * K), .k = (int64_t)((uint64_t)x->k * K) };
			r.exact = x->exact && (in.ty != VT_I32 || ((in.fl & VIF_NSW) && !x->uns));
			if (!r.bs)
				r.bv = 0;
			if (allinv && !r.exact)
				vv_opaque(S, q, &r);
			else if (!r.exact && in.ty != VT_I32)
				r.kind = VK_BAD;
		} else if (allinv) {
			vv_opaque(S, q, &r);
		} else {
			r.kind = in.op == VO_MUL && in.ty == VT_I32 && vv_lane(o[0]) && vv_lane(o[1]) ? VK_VEC : VK_BAD;
		}
		break;
	}
	case VO_AND: case VO_OR: case VO_XOR: case VO_NEG: case VO_NOT:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = in.ty == VT_I32 && vv_lane(o[0]) && (no < 2 || vv_lane(o[1])) ? VK_VEC : VK_BAD;
		break;
	case VO_EXT:
		if (VIF_W(in.fl) == 2 && in.ty == VT_I64 && V->p[w[0]].in.ty == VT_I32 && vv_lin(o[0]) && o[0]->exact &&
		    ((in.fl & VIF_MS) ? !o[0]->uns : o[0]->uns)) {	/* (the extension that keeps its form's value) */
			r = *o[0];
			r.nuse = 0, r.sce = r.scb = r.sc0 = r.vec = 0, r.uns = 0;
		} else if (allinv) {
			vv_opaque(S, q, &r);
		} else {
			r.kind = VK_BAD;
		}
		break;
	case VO_I2P:	/* an address computed as an integer: its invariant term is the object */
		if (vv_lin(o[0]) && o[0]->exact && (o[0]->objk || (o[0]->bv && o[0]->bs == 1))) {
			r = *o[0];
			r.nuse = 0, r.sce = r.scb = r.sc0 = r.vec = 0;
			if (!r.objk)
				r.objk = VJ_PTR, r.objv = r.bv, r.bv = 0, r.bs = 0;
		} else if (allinv) {
			vv_opaque(S, q, &r);
		} else {
			r.kind = VK_BAD;
		}
		break;
	case VO_TRUNC:
		if (allinv)
			vv_opaque(S, q, &r);
		else if (o[0]->kind == VK_LIN)
			r = (vvi){ .kind = VK_LIN, .c = (int32_t)o[0]->c };
		else
			r.kind = VK_BAD;
		break;
	case VO_ICMP:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = V->p[w[0]].in.ty == VT_I32 && vv_lane(o[0]) && vv_lane(o[1]) ? VK_MASK : VK_BAD;
		break;
	case VO_FCMP:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = V->p[w[0]].in.ty == VT_F32 && vv_lane(o[0]) && vv_lane(o[1]) ? VK_MASK : VK_BAD;
		break;
	case VO_SELECT:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = o[0]->kind == VK_MASK && (in.ty == VT_I32 || in.ty == VT_F32) && vv_lane(o[1]) && vv_lane(o[2]) ? VK_VEC : VK_BAD;
		break;
	case VO_FADD: case VO_FSUB: case VO_FMUL: case VO_FDIV: case VO_FNEG: case VO_FABS: case VO_FSQRT:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = in.ty == VT_F32 && vv_lane(o[0]) && (no < 2 || vv_lane(o[1])) ? VK_VEC : VK_BAD;
		break;
	case VO_S2F:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = in.ty == VT_F32 && V->p[w[0]].in.ty == VT_I32 && vv_lane(o[0]) ? VK_VEC : VK_BAD;
		break;
	case VO_SDIV: case VO_UDIV: case VO_SREM: case VO_UREM: case VO_LSHR: case VO_ASHR: case VO_UMULHI: case VO_SMULHI:
	case VO_CLZ: case VO_CTZ: case VO_POPCNT: case VO_BSWAP: case VO_U2F: case VO_F2S: case VO_F2U: case VO_FEXT:
	case VO_FTRUNC: case VO_BITCAST: case VO_PSUB: case VO_P2I:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = VK_BAD;
		break;
	case VO_LOAD:
		if (in.fl & VIF_VOL)
			return vv_no(S, "a volatile load");
		if (!vv_lin(o[0]) || !o[0]->exact || !o[0]->objk) {
			r.kind = VK_BAD;
		} else if (o[0]->kind == VK_INV) {
			if (S->ninvld == 64)
				return vv_no(S, "too many invariant loads");
			S->invld[S->ninvld++] = q;
			vv_opaque(S, q, &r);
		} else if (VIF_W(in.fl) == 2 && (in.ty == VT_F32 || in.ty == VT_I32) && (o[0]->c == 4 || o[0]->c == -4)) {
			if (S->nacc == 64)
				return vv_no(S, "too many accesses");
			S->acc[S->nacc++] = (vvacc){ q, S->pos, false };
			r.kind = VK_VEC;
		} else {
			r.kind = VK_BAD;
		}
		break;
	case VO_STORE: {
		uint32_t vt = V->p[w[1]].in.ty;

		if (in.fl & VIF_VOL)
			return vv_no(S, "a volatile store");
		if (!vv_lin(o[0]) || !o[0]->exact || !o[0]->objk)
			return vv_no(S, "a store whose address is not base + linear");
		if (o[0]->c != 4 && o[0]->c != -4)
			return vv_no(S, o[0]->c ? "a store with a stride other than its size" : "a store to one address");
		if (VIF_W(in.fl) != 2 || (vt != VT_F32 && vt != VT_I32))
			return vv_no(S, "a store that is not 4 bytes");
		if (!vv_lane(o[1]))
			return vv_no(S, "a stored value that cannot be vectorized");
		if (S->nacc == 64)
			return vv_no(S, "too many accesses");
		S->acc[S->nacc++] = (vvacc){ q, S->pos, true };
		r.kind = VK_BAD;	/* (no value) */
		break;
	}
	default:
		return vv_no(S, "an instruction with effects (a call, a block copy, an atomic)");
	}
done:
	{
		vvi *x = vv_touch(S, q);
		uint16_t nu = x->nuse;

		*x = r;
		x->nuse = nu;
	}
	S->pos++;
	return true;
}

/* the ranges of x and y must be checked apart when the loop is entered (at most 8 pairs, as LLVM) */
static bool vv_chk(vvs *S, uint32_t x, uint32_t y)
{
	for (uint32_t i = 0; i < S->nchk; i++)
		if (S->chk[i].a == x && S->chk[i].b == y)
			return true;
	if (S->nchk == 8)
		return vv_no(S, "more than 8 pairs of accesses to tell apart at run time");
	S->chk[S->nchk].a = x, S->chk[S->nchk++].b = y;
	return true;
}

/* 6.3: every store against every other access.  Two different symbols never meet; one object
 * (a symbol, or one pointer value) is decided here by the distance; anything else by a range
 * check when the loop is entered */
static bool vv_depend(vvs *S)
{
	vwst *V = S->e->vw;

	for (uint32_t i = 0; i < S->ninvld; i++) {
		const vvi *a = vv_get(S, vw_find(V, V->p[S->invld[i]].in.a));

		for (uint32_t j = 0; j < S->nacc; j++) {
			const vvi *s = vv_get(S, vw_find(V, V->p[S->acc[j].q].in.a));

			if (!S->acc[j].store || (a->objk == VJ_SYM && s->objk == VJ_SYM && s->objv != a->objv))
				continue;
			if (!vv_chk(S, j, 64 + i))
				return false;
		}
	}
	for (uint32_t i = 0; i < S->nacc; i++) {
		const vvi *s = vv_get(S, vw_find(V, V->p[S->acc[i].q].in.a));

		if (!S->acc[i].store)
			continue;
		for (uint32_t j = 0; j < S->nacc; j++) {
			const vvi *a = vv_get(S, vw_find(V, V->p[S->acc[j].q].in.a));
			int64_t d, g;
			const vvacc *E, *Lt;

			if (j == i || (S->acc[j].store && j < i))
				continue;
			if (a->objk != s->objk || a->objv != s->objv) {
				if (a->objk == VJ_SYM && s->objk == VJ_SYM)
					continue;
				if (!vv_chk(S, i, j))
					return false;
				continue;
			}
			if (a->c != s->c || a->bv != s->bv || (a->bv && a->bs != s->bs)) {	/* no constant distance: their ranges */
				if (!vv_chk(S, i, j))
					return false;
				continue;
			}
			if ((s->k - a->k) % 4)
				return vv_no(S, "two accesses of one array that overlap in part");
			d = (s->k - a->k) / s->c;	/* a touches the store's element d iterations later */
			g = d < 0 ? -d : d;
			if (!d || g >= 4)
				continue;
			E = d > 0 ? &S->acc[i] : &S->acc[j], Lt = d > 0 ? &S->acc[j] : &S->acc[i];
			if (E->store && !Lt->store)
				return vv_no(S, "a value stored and read back 1-3 iterations later");
			if (E->pos > Lt->pos)
				return vv_no(S, "an access of a later iteration that comes first in the body");
		}
	}
	return true;
}

/* ---- building ---- */

static uint32_t vv_ins(vvs *S, uint32_t b, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t bb, uint32_t c)
{
	return vw_ins_in(S->e, b, op, ty, fl, a, bb, c);
}

enum { VM_ENTRY, VM_BODY, VM_T0 };
static uint32_t vv_scalar(vvs *S, uint32_t v, int mode);

/* a copy of pure instruction or load v in block b, its operands through vv_scalar */
static uint32_t vv_clone(vvs *S, uint32_t v, uint32_t b, int mode)
{
	vwst *V = S->e->vw;
	vir_ins in = V->p[v].in;
	const vir_opinfo *oi = &vir_optab[in.op];
	uint32_t w[3] = { in.a, in.b, in.c };

	for (int k = 0; k < 3; k++)
		if (vw_isval(oi->k[k]) && !(w[k] = vv_scalar(S, w[k], mode)))
			return 0;
	return vv_ins(S, b, in.op, in.ty, in.fl, w[0], w[1], w[2]);
}

/* v as a scalar.  VM_ENTRY: at the loop's entry, in VPRE (only what the header computes: VPRE
 * runs whatever the trip count).  VM_BODY: in the vector body, for its first lane.  VM_T0: in
 * VPH, at the first iteration.  An invariant of the loop is made once, in VPH */
static uint32_t vv_scalar(vvs *S, uint32_t v, int mode)
{
	vwst *V = S->e->vw;
	vvi *x;
	uint32_t r;

	v = vw_find(V, v);
	x = vv_get(S, v);
	if (v >= S->n0 || !vv_lin(x)) {
		vv_no(S, "a value that is neither invariant nor linear, as a scalar");
		return 0;
	}
	if (V->p[v].blk == VW_NOBLK || !S->inl[V->p[v].blk])
		return v;
	if (x->sce)
		return x->sce;	/* (VPRE dominates the rest) */
	if (mode != VM_ENTRY && x->scb && (mode == VM_BODY || x->kind == VK_INV))
		return x->scb;
	if (mode == VM_T0 && x->sc0)
		return x->sc0;
	if (V->p[v].in.op == VO_BPARAM) {	/* an induction */
		for (uint32_t i = 0; i < S->nind; i++)
			if (S->ind[i] == v)
				return mode == VM_BODY ? S->indb[i] : vw_find(V, V->x[V->p[v].in.b + S->jP]);
		vv_no(S, "a parameter that is not an induction");
		return 0;
	}
	if (mode == VM_ENTRY && V->p[v].blk != S->H) {
		vv_no(S, "the trip count needs a value of the body");
		return 0;
	}
	r = vv_clone(S, v, mode == VM_ENTRY ? S->VPRE : x->kind == VK_INV || mode == VM_T0 ? S->VPH : S->VB, mode);
	x = &S->I[v];
	if (mode == VM_ENTRY)
		x->sce = r;
	else if (x->kind == VK_INV || mode == VM_BODY)
		x->scb = r;
	else
		x->sc0 = r;
	return r;
}

static uint32_t vv_vty(uint32_t ty) { return ty == VT_F32 ? VT_F32X4 : ty == VT_I32 ? VT_I32X4 : 0; }

/* lanes 0, c, 2c, 3c */
static uint32_t vv_step(vvs *S, int64_t c)
{
	uint32_t s, k;

	for (uint32_t i = 0; i < S->nstep; i++)
		if (S->stepc[i] == c)
			return S->stepv[i];
	s = vv_ins(S, S->VPH, VO_VSERIES, VT_I32X4, 0, 0, 0, 0);
	if ((int32_t)c != 1) {
		k = vv_ins(S, S->VPH, VO_VSPLAT, VT_I32X4, 0, vw_iconst(S->e, VT_I32, (int32_t)c), 0, 0);
		s = vv_ins(S, S->VPH, VO_MUL, VT_I32X4, 0, s, k, 0);
	}
	if (S->nstep < 4)
		S->stepc[S->nstep] = c, S->stepv[S->nstep++] = s;
	return s;
}

static uint32_t vv_vec(vvs *S, uint32_t v);

static uint32_t vv_mask(vvs *S, uint32_t v)
{
	vwst *V = S->e->vw;
	vir_ins in;
	uint32_t a, b, r;
	vvi *x;

	v = vw_find(V, v);
	x = vv_get(S, v);
	if (x->kind != VK_MASK) {
		vv_no(S, "a condition that is not a comparison of lanes");
		return 0;
	}
	if (x->vec)
		return x->vec;
	in = V->p[v].in;
	if (!(a = vv_vec(S, in.a)) || !(b = vv_vec(S, in.b)))
		return 0;
	r = vv_ins(S, S->VB, in.op == VO_ICMP ? VO_VICMP : VO_VFCMP, VT_I32X4, in.fl & 15, a, b, 0);
	S->I[v].vec = r;
	return r;
}

/* v's four lanes */
static uint32_t vv_vec(vvs *S, uint32_t v)
{
	vwst *V = S->e->vw;
	vvi *x;
	vir_ins in;
	uint32_t ty, vt, r = 0, a, b, c;

	v = vw_find(V, v);
	x = vv_get(S, v);
	if (v < S->n0 && x->vec)
		return x->vec;
	ty = V->p[v].in.ty, vt = vv_vty(ty);
	if (!vt || v >= S->n0) {
		vv_no(S, "a lane that is not 32 bits");
		return 0;
	}
	in = V->p[v].in;
	switch (x->kind) {
	case VK_INV:
		if (!(a = vv_scalar(S, v, VM_BODY)))
			return 0;
		r = vv_ins(S, S->VPH, VO_VSPLAT, vt, 0, a, 0, 0);
		break;
	case VK_LIN: {
		int64_t cc = x->c;

		if (ty != VT_I32 || !(a = vv_scalar(S, v, VM_BODY)))
			return vv_no(S, "a linear value that is not an i32, as lanes"), 0;
		a = vv_ins(S, S->VB, VO_VSPLAT, VT_I32X4, 0, a, 0, 0);
		r = vv_ins(S, S->VB, VO_ADD, VT_I32X4, 0, a, vv_step(S, cc), 0);
		break;
	}
	case VK_VEC:
		switch (in.op) {
		case VO_ADD: case VO_SUB: case VO_MUL: case VO_AND: case VO_OR: case VO_XOR:
		case VO_FADD: case VO_FSUB: case VO_FMUL: case VO_FDIV:
			if (!(a = vv_vec(S, in.a)) || !(b = vv_vec(S, in.b)))
				return 0;
			r = vv_ins(S, S->VB, in.op, vt, 0, a, b, 0);
			break;
		case VO_NEG: case VO_NOT: case VO_FNEG: case VO_FABS: case VO_FSQRT:
			if (!(a = vv_vec(S, in.a)))
				return 0;
			r = vv_ins(S, S->VB, in.op, vt, 0, a, 0, 0);
			break;
		case VO_S2F:
			if (!(a = vv_vec(S, in.a)))
				return 0;
			r = vv_ins(S, S->VB, VO_VS2F, VT_F32X4, 0, a, 0, 0);
			break;
		case VO_SELECT:
			if (!(a = vv_mask(S, in.a)) || !(b = vv_vec(S, in.b)) || !(c = vv_vec(S, in.c)))
				return 0;
			r = vv_ins(S, S->VB, VO_VBITSEL, vt, 0, a, b, c);
			break;
		default:	/* (a load: made where the scalar loop has it) */
			return vv_no(S, "a lane used before it is made"), 0;
		}
		break;
	default:
		return vv_no(S, x->kind == VK_RED || x->kind == VK_REDUPD ? "a sum used inside the loop" : "a value that cannot be vectorized"), 0;
	}
	S->I[v].vec = r;
	return r;
}

static uint32_t vv_param(vvs *S, uint32_t b, uint32_t ty, uint32_t x0, uint32_t x1, uint32_t x2);

/* the address of the four elements load or store q touches in one go, and whether they are
 * reversed: a pointer of the vector body that steps 16 bytes (the lowest of the four addresses) */
static uint32_t vv_addr(vvs *S, uint32_t q, bool *rev)
{
	vwst *V = S->e->vw;
	uint32_t a = vw_find(V, V->p[q].in.a), s;

	*rev = vv_get(S, a)->c < 0;
	for (uint32_t i = 0; i < S->npin; i++)
		if (S->pin[i].a == a)
			return S->pin[i].par;
	if (S->npin == 32)
		return vv_no(S, "too many addresses"), 0;
	if (!(s = vv_scalar(S, a, VM_T0)))
		return 0;
	if (*rev)
		s = vv_ins(S, S->VPH, VO_PTRADD, VT_PTR, 0, s, vw_iconst(S->e, VT_I64, -12), 0);
	S->pin[S->npin].a = a, S->pin[S->npin].rev = *rev;
	return S->pin[S->npin++].par = vv_param(S, S->VB, VT_PTR, s, 0, 0);
}

/* [lo, hi): the bytes access x (vv_chk's index) touches in the vector iterations; span = 4 * nv */
static bool vv_range(vvs *S, uint32_t x, uint32_t span, uint32_t *lo, uint32_t *hi)
{
	vwst *V = S->e->vw;
	uint32_t q = x >= 64 ? S->invld[x - 64] : S->acc[x].q, a = vw_find(V, V->p[q].in.a), s = vv_scalar(S, a, VM_T0);

	if (!s)
		return false;
	if (x >= 64) {
		*lo = s, *hi = vv_ins(S, S->VPH, VO_PTRADD, VT_PTR, 0, s, vw_iconst(S->e, VT_I64, 1 << VIF_W(V->p[q].in.fl)), 0);
	} else if (vv_get(S, a)->c > 0) {
		*lo = s, *hi = vv_ins(S, S->VPH, VO_PTRADD, VT_PTR, 0, s, span, 0);
	} else {
		*hi = vv_ins(S, S->VPH, VO_PTRADD, VT_PTR, 0, s, vw_iconst(S->e, VT_I64, 4), 0);
		*lo = vv_ins(S, S->VPH, VO_PTRADD, VT_PTR, 0, *hi, vv_ins(S, S->VPH, VO_NEG, VT_I64, 0, span, 0, 0), 0);
	}
	return true;
}

/* a parameter of b with its operands per predecessor (b's first two or three) */
static uint32_t vv_param(vvs *S, uint32_t b, uint32_t ty, uint32_t x0, uint32_t x1, uint32_t x2)
{
	uint32_t p = vw_newphi(S->e, b, 0, ty), w[3] = { x0, x1, x2 };

	S->e->vw->p[p].in.b = vw_xput(S->e, w, 3);
	return p;
}

static uint32_t vv_blk(vvs *S, uint32_t p0, uint32_t p1)
{
	ecx *e = S->e;
	vwst *V = e->vw;
	uint32_t b = vw_newblk(e), n = (p0 != VW_NOBLK) + (p1 != VW_NOBLK);

	GROW(V->pr, V->npr, V->cpr, 2);
	V->b[b].pred = V->npr, V->b[b].npred = n;
	if (p0 != VW_NOBLK)
		V->pr[V->npr++] = p0;
	if (p1 != VW_NOBLK)
		V->pr[V->npr++] = p1;
	return b;
}

static void vv_term(vvs *S, uint32_t b, uint32_t op, uint32_t x, uint32_t y, uint32_t z)
{
	uint32_t t = vw_put(S->e, op, VT_VOID, 0, x, y, z, b);

	S->e->vw->b[b].term = t;
}

/* nx = p + *K: adds and subtracts of constants on the way from parameter p to its next value.
 * A step that is a value from outside the loop counts as 1 and is noted in S->one */
static bool vv_chain(vvs *S, uint32_t p, uint32_t nx, int64_t *K, bool *nsw)
{
	vwst *V = S->e->vw;
	uint32_t v = nx, none0 = S->none;

	*K = 0, *nsw = true;
	for (int n = 0; v != p; n++) {
		vir_ins N;
		uint32_t a, b;
		int64_t k;

		if (n == 8 || v >= S->n0 || V->p[v].blk == VW_NOBLK || !S->inl[V->p[v].blk])
			goto no;
		N = V->p[v].in;
		if (N.op != VO_ADD && N.op != VO_SUB && N.op != VO_PTRADD)
			goto no;
		a = vw_find(V, N.a), b = vw_find(V, N.b);
		if (vw_kint(V, b, &k)) {
			*K += N.op == VO_SUB ? -k : k;
		} else if (N.op == VO_ADD && b < S->n0 && V->p[b].in.op != VO_ICONST && (V->p[b].blk == VW_NOBLK || !S->inl[V->p[b].blk]) && S->none < 8) {
			S->one[S->none++] = b;
			*K += 1;
		} else {
			goto no;
		}
		if (N.ty == VT_I32 && !(N.fl & VIF_NSW))
			*nsw = false;
		v = a;
	}
	if (*K)
		return true;
no:
	S->none = none0;
	return false;
}

/* the loop goes on while parameter p is below an invariant, unsigned: the header's test */
static bool vv_ultctl(vvs *S, uint32_t p)
{
	vwst *V = S->e->vw;
	uint32_t t = V->b[S->H].term, c = vw_find(V, V->p[t].in.a), cc;

	if (c >= S->n0 || V->p[c].in.op != VO_ICMP || V->p[c].blk != S->H)
		return false;
	cc = V->p[c].in.fl & 15;
	if (V->p[t].in.b == S->X)
		cc = (uint32_t[]){ 1, 0, 5, 4, 3, 2, 9, 8, 7, 6 }[cc];
	return (cc == 6 && vw_find(V, V->p[c].in.a) == p) || (cc == 8 && vw_find(V, V->p[c].in.b) == p);
}

/* the loop headed by H (S->lb, P, L, X set): analysed and, when it can be, vectorized */
static bool vv_loop(vvs *S)
{
	ecx *e = S->e;
	vwst *V = e->vw;
	uint32_t H = S->H, t = V->b[H].term, cond = vw_find(V, V->p[t].in.a), u, n, cc, d, cnt, cntn, nv, guard, z = 0;
	bool incl;
	int64_t c, ac;
	vvi *ui, *ni;
	uint32_t jp[40], njp = 0;	/* J's parameter per live parameter of H, in H's order */

	S->nacc = S->ninvld = S->nred = S->nind = S->nstep = S->nchk = S->npin = S->none = 0, S->pos = 0, S->why = NULL;
	/* H's parameters */
	for (uint32_t p = V->b[H].phead; p; p = V->p[p].next) {
		uint32_t init, nx, ty = V->p[p].in.ty;
		vir_ins N;
		vvi *x, r = { 0 };
		int64_t K = 0;
		bool kb;

		if (V->fwd[p] != p)
			continue;
		init = vw_find(V, V->x[V->p[p].in.b + S->jP]), nx = vw_find(V, V->x[V->p[p].in.b + S->jL]);
		if (init >= S->n0 || nx >= S->n0 || V->p[nx].blk == VW_NOBLK || !S->inl[V->p[nx].blk] || V->p[nx].in.op == VO_BPARAM)
			return vv_no(S, "a value carried around the loop that is neither an induction nor a sum");
		if (V->p[init].blk != VW_NOBLK && S->inl[V->p[init].blk])
			return vv_no(S, "an initial value from inside the loop");
		N = V->p[nx].in;
		if (vv_chain(S, p, nx, &K, &kb)) {
			vvi *ii = vv_get(S, init), kk = { .kind = VK_INV, .exact = 1, .c = K };
			bool rep = vv_comb(&r, ii, &kk, 1);

			if (S->nind == 32)
				return vv_no(S, "too many inductions");
			r.kind = VK_LIN, r.exact = rep && ii->exact && (ty != VT_I32 || kb);
			if (!r.exact && rep && ii->exact && K == 1 && vv_ultctl(S, p) && (!vv_isk(ii) || ii->k >= 0))
				r.exact = r.uns = 1;	/* (below an unsigned bound, stepping 1: it cannot wrap) */
			if (!r.exact && ty != VT_I32)
				return vv_no(S, "an induction without a linear form");
			S->indk[S->nind] = K;
			S->ind[S->nind++] = p;
		} else if (ty == VT_I32 && ((N.op == VO_ADD && (vw_find(V, N.a) == p || vw_find(V, N.b) == p)) || (N.op == VO_SUB && vw_find(V, N.a) == p))) {
			if (S->nred == 8)
				return vv_no(S, "too many sums");
			S->red[S->nred++] = (vvred){ p, nx, vw_find(V, N.a) == p ? N.b : N.a, 0, 0 };
			r.kind = VK_RED;
		} else {
			return vv_no(S, ty == VT_F32 || ty == VT_F64 ? "a float carried around the loop (a reduction or a recurrence: order matters)" :
					"a value carried around the loop that is neither an induction nor a sum");
		}
		x = vv_touch(S, p);
		*x = r;
	}
	/* the instructions, in the order they run */
	for (uint32_t i = 0; i < S->nlb; i++) {
		uint32_t b = S->lb[i], tt = V->b[b].term;

		for (uint32_t q = V->b[b].head; q; q = V->p[q].next)
			if (!vv_skip(V, q) && !vv_classify(S, q))
				return false;
		if (V->p[tt].in.op == VO_BRIF) {
			vvi *x = vv_get(S, vw_find(V, V->p[tt].in.a));

			if (x->nuse < 0xffff)
				x->nuse++;
		}
	}
	if (!S->nacc && !S->nred)
		return vv_no(S, "nothing to vectorize: no store and no sum");
	{
		bool st = S->nred != 0;

		for (uint32_t i = 0; i < S->nacc; i++)
			st |= S->acc[i].store;
		if (!st)
			return vv_no(S, "nothing to vectorize: no store and no sum");
	}
	for (uint32_t i = 0; i < S->nred; i++)
		if (vv_get(S, S->red[i].p)->nuse != 1 || vv_get(S, S->red[i].upd)->nuse != 0)
			return vv_no(S, "a sum used inside the loop");
	if (!vv_depend(S))
		return false;
	/* the exit test: continue while u cc n */
	if (cond >= S->n0 || V->p[cond].in.op != VO_ICMP || V->p[cond].blk != H)
		return vv_no(S, "an exit test that is not a comparison in the header");
	u = vw_find(V, V->p[cond].in.a), n = vw_find(V, V->p[cond].in.b), cc = V->p[cond].in.fl & 15;
	{
		static const uint8_t inv[10] = { 1, 0, 5, 4, 3, 2, 9, 8, 7, 6 }, sw[10] = { 0, 1, 4, 5, 2, 3, 8, 9, 6, 7 };

		if (V->p[t].in.b == S->X)
			cc = inv[cc];	/* (the loop goes on when the test fails) */
		if (vv_get(S, u)->kind == VK_INV) {
			uint32_t x = u;

			u = n, n = x, cc = sw[cc];
		}
	}
	ui = vv_get(S, u), ni = vv_get(S, n);
	if (ui->kind != VK_LIN || !ui->exact || ni->kind != VK_INV || (V->p[u].in.ty != VT_I32 && V->p[u].in.ty != VT_I64))
		return vv_no(S, "an exit test that is not linear against invariant");
	c = ui->c;
	if (c > 0 && (cc == 2 || cc == 3) && !ui->uns)
		incl = cc == 3;
	else if (c < 0 && (cc == 4 || cc == 5) && !ui->uns)
		incl = cc == 5;
	else if (c > 0 && cc == 6 && (ui->uns || V->p[u].in.ty == VT_I64))
		incl = false;
	else
		return vv_no(S, "an exit test that is not <, <=, >, >= in the direction of the step");
	ac = c < 0 ? -c : c;
	if (ac > (1 << 20))
		return vv_no(S, "a step too large");

	/* ---- build ---- */
	e->curloc = V->p[t].loc;
	S->VPRE = vv_blk(S, S->P, VW_NOBLK);
	S->VPH = vv_blk(S, S->VPRE, VW_NOBLK);
	S->VB = vv_blk(S, S->VPH, VW_NOBLK);
	V->pr[V->b[S->VB].pred + 1] = S->VB, V->b[S->VB].npred = 2, V->npr = V->b[S->VB].pred + 2;
	GROW(V->pr, V->npr, V->cpr, 1);
	S->VX = vv_blk(S, S->VB, VW_NOBLK);
	S->J = vv_blk(S, S->VPRE, S->VX);
	if (S->nchk) {	/* (J's predecessors are the last in V->pr) */
		GROW(V->pr, V->npr, V->cpr, 1);
		V->pr[V->npr++] = S->VPH, V->b[S->J].npred = 3;
	}
	/* VPRE: the span */
	{
		uint32_t u0 = vv_scalar(S, u, VM_ENTRY), n0 = vv_scalar(S, n, VM_ENTRY);

		if (!u0 || !n0)
			return false;
		uint32_t more = 0;

		if (V->p[u].in.ty == VT_I32) {
			u0 = vv_ins(S, S->VPRE, VO_EXT, VT_I64, 2 | (cc == 6 ? 0 : VIF_MS), u0, 0, 0);
			n0 = vv_ins(S, S->VPRE, VO_EXT, VT_I64, 2 | (cc == 6 ? 0 : VIF_MS), n0, 0, 0);
		} else if (cc == 6) {	/* (64 bits, unsigned: the span is n - u only when u <= n) */
			more = vv_ins(S, S->VPRE, VO_ICMP, VT_I32, 7, u0, n0, 0);
		}
		for (uint32_t i = 0; i < S->none; i++) {	/* a step that is a value: 1 */
			uint32_t o = S->one[i], ot = V->p[o].in.ty;
			uint32_t m = vv_ins(S, S->VPRE, VO_ICMP, VT_I32, 0, o, vw_iconst(e, ot == VT_I32 ? VT_I32 : VT_I64, 1), 0);

			more = more ? vv_ins(S, S->VPRE, VO_AND, VT_I32, 0, more, m, 0) : m;
		}
		d = c > 0 ? vv_ins(S, S->VPRE, VO_SUB, VT_I64, 0, n0, u0, 0) : vv_ins(S, S->VPRE, VO_SUB, VT_I64, 0, u0, n0, 0);
		if (incl)
			d = vv_ins(S, S->VPRE, VO_ADD, VT_I64, 0, d, vw_iconst(e, VT_I64, 1), 0);
		guard = vv_ins(S, S->VPRE, VO_ICMP, VT_I32, 5, d, vw_iconst(e, VT_I64, 4 * ac), 0);
		if (more)
			guard = vv_ins(S, S->VPRE, VO_AND, VT_I32, 0, guard, more, 0);
		vv_term(S, S->VPRE, VO_BRIF, guard, S->VPH, S->J);
	}
	/* VPH: the iterations done on lanes */
	nv = d;
	if (ac != 1) {
		nv = vv_ins(S, S->VPH, VO_ADD, VT_I64, 0, nv, vw_iconst(e, VT_I64, ac - 1), 0);
		nv = vv_ins(S, S->VPH, VO_UDIV, VT_I64, 0, nv, vw_iconst(e, VT_I64, ac), 0);
	}
	nv = vv_ins(S, S->VPH, VO_AND, VT_I64, 0, nv, vw_iconst(e, VT_I64, -4), 0);
	/* VB's parameters: operand 0 from VPH, 1 from VB (filled at the end) */
	cnt = vv_param(S, S->VB, VT_I64, nv, 0, 0);
	for (uint32_t i = 0; i < S->nind; i++) {
		uint32_t p = S->ind[i];

		S->indb[i] = vv_param(S, S->VB, V->p[p].in.ty, vw_find(V, V->x[V->p[p].in.b + S->jP]), 0, 0);
	}
	for (uint32_t i = 0; i < S->nred; i++) {
		if (!z)
			z = vv_ins(S, S->VPH, VO_VSPLAT, VT_I32X4, 0, vw_iconst(e, VT_I32, 0), 0, 0);
		S->red[i].acc = vv_param(S, S->VB, VT_I32X4, z, 0, 0);
	}
	/* the body */
	for (uint32_t i = 0; i < S->nlb && !S->why; i++)
		for (uint32_t q = V->b[S->lb[i]].head; q && !S->why; q = V->p[q].next) {
			vir_ins in;
			bool rev;
			uint32_t a, x;

			if (q >= S->n0 || vv_skip(V, q))
				continue;
			in = V->p[q].in;
			if (in.op == VO_LOAD && S->I[q].kind == VK_VEC) {
				if (!(a = vv_addr(S, q, &rev)))
					break;
				x = vv_ins(S, S->VB, VO_LOAD, vv_vty(in.ty), in.fl & ~(uint32_t)VIF_MS, a, in.b, 0);
				if (rev)
					x = vv_ins(S, S->VB, VO_VREV, vv_vty(in.ty), 0, x, 0, 0);
				S->I[q].vec = x;
			} else if (in.op == VO_STORE) {
				if (!(x = vv_vec(S, in.b)) || !(a = vv_addr(S, q, &rev)))
					break;
				if (rev)
					x = vv_ins(S, S->VB, VO_VREV, V->p[x].in.ty, 0, x, 0, 0);
				vv_ins(S, S->VB, VO_STORE, VT_VOID, in.fl, a, x, 0);
			} else if (S->I[q].kind == VK_REDUPD) {
				for (uint32_t k = 0; k < S->nred; k++)
					if (S->red[k].upd == q) {
						if (!(x = vv_vec(S, S->red[k].x)))
							break;
						S->red[k].accnext = vv_ins(S, S->VB, in.op, VT_I32X4, 0, S->red[k].acc, x, 0);
					}
			}
		}
	if (S->why)
		return false;
	/* the inductions step four iterations; the counter */
	for (uint32_t i = 0; i < S->nind; i++) {
		uint32_t p = S->ind[i], ty = V->p[p].in.ty;

		S->indn[i] = vv_ins(S, S->VB, ty == VT_PTR ? VO_PTRADD : VO_ADD, ty, 0, S->indb[i],
				    vw_iconst(e, ty == VT_I32 ? VT_I32 : VT_I64, (int64_t)((uint64_t)S->indk[i] * 4)), 0);
		V->x[V->p[S->indb[i]].in.b + 1] = S->indn[i];
	}
	for (uint32_t i = 0; i < S->nred; i++)
		V->x[V->p[S->red[i].acc].in.b + 1] = S->red[i].accnext;
	for (uint32_t i = 0; i < S->npin; i++)
		V->x[V->p[S->pin[i].par].in.b + 1] = vv_ins(S, S->VB, VO_PTRADD, VT_PTR, 0, S->pin[i].par, vw_iconst(e, VT_I64, S->pin[i].rev ? -16 : 16), 0);
	cntn = vv_ins(S, S->VB, VO_ADD, VT_I64, 0, cnt, vw_iconst(e, VT_I64, -4), 0);
	V->x[V->p[cnt].in.b + 1] = cntn;
	guard = vv_ins(S, S->VB, VO_ICMP, VT_I32, 1, cntn, vw_iconst(e, VT_I64, 0), 0);
	vv_term(S, S->VB, VO_BRIF, guard, S->VB, S->VX);
	/* VX and J: H's parameters after the vector iterations (operand 0: none were run) */
	for (uint32_t p = V->b[H].phead; p; p = V->p[p].next) {
		uint32_t init, fin = 0;

		if (V->fwd[p] != p)
			continue;
		init = vw_find(V, V->x[V->p[p].in.b + S->jP]);
		for (uint32_t i = 0; i < S->nind; i++)
			if (S->ind[i] == p)
				fin = S->indn[i];
		for (uint32_t i = 0; i < S->nred; i++)
			if (S->red[i].p == p) {
				fin = vv_ins(S, S->VX, VO_VREDUCE, VT_I32, 0, S->red[i].accnext, 0, 0);
				fin = vv_ins(S, S->VX, VO_ADD, VT_I32, 0, init, fin, 0);
			}
		if (!fin || njp == 40)
			return vv_no(S, "a parameter without a final value");
		jp[njp++] = vv_param(S, S->J, V->p[p].in.ty, init, fin, init);
	}
	vv_term(S, S->VX, VO_BR, S->J, 0, 0);
	vv_term(S, S->J, VO_BR, H, 0, 0);
	if (S->nchk) {	/* the pairs that may be one memory: any two ranges that meet, and the loop stays scalar */
		uint32_t span = vv_ins(S, S->VPH, VO_SHL, VT_I64, 0, nv, vw_iconst(e, VT_I64, 2), 0), any = 0;

		for (uint32_t i = 0; i < S->nchk; i++) {
			uint32_t l0, h0, l1, h1, m;

			if (!vv_range(S, S->chk[i].a, span, &l0, &h0) || !vv_range(S, S->chk[i].b, span, &l1, &h1))
				return false;
			m = vv_ins(S, S->VPH, VO_AND, VT_I32, 0, vv_ins(S, S->VPH, VO_ICMP, VT_I32, 6, l0, h1, 0),
				   vv_ins(S, S->VPH, VO_ICMP, VT_I32, 6, l1, h0, 0), 0);
			any = any ? vv_ins(S, S->VPH, VO_OR, VT_I32, 0, any, m, 0) : m;
		}
		vv_term(S, S->VPH, VO_BRIF, any, S->J, S->VB);
	} else {
		vv_term(S, S->VPH, VO_BR, S->VB, 0, 0);
	}
	/* commit: P enters at VPRE; H's entry is J */
	{
		uint32_t ns = vw_nsucc(V, S->P), k = 0;

		for (uint32_t i = 0; i < ns; i++)
			if (*vw_succp(V, S->P, i) == H)
				*vw_succp(V, S->P, i) = S->VPRE;
		for (uint32_t p = V->b[H].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p)
				V->x[V->p[p].in.b + S->jP] = jp[k++];
		V->pr[V->b[H].pred + S->jP] = S->J;
	}
	return true;
}

/* the new blocks into the layout, in front of H */
static void vv_place(vvs *S)
{
	ecx *e = S->e;
	vwst *V = e->vw;
	const uint32_t nb[5] = { S->VPRE, S->VPH, S->VB, S->VX, S->J };
	uint32_t at = 0;

	GROW(V->order, V->norder, V->corder, 5);
	while (at < V->norder && V->order[at] != S->H)
		at++;
	memmove(&V->order[at + 5], &V->order[at], (size_t)(V->norder - at) * sizeof *V->order);
	for (int i = 0; i < 5; i++) {
		V->order[at + i] = nb[i];
		V->b[nb[i]].reach = 1;
	}
	V->norder += 5;
}

static int vv_env(void)
{
	static int m = -1;

	if (m < 0) {
		const char *s = getenv("VCC_VIR_VEC");	/* 0: off; r: report each loop to stderr */

		m = s && !strcmp(s, "0") ? 0 : s && strchr(s, 'r') ? 3 : 1;
	}
	return m;
}

static void vw_vectorize(ecx *e)
{
	vwst *V = e->vw;
	vvs S = { .e = e, .n0 = V->np };
	uint32_t n0 = V->norder, *hs, nh = 0, loc = e->curloc;
	char nm[100];

	if (!vv_env() || e->E->o->general_regs_only || !vw_enabled(e->E->o))
		return;
	hs = malloc(((size_t)n0 + 1) * sizeof *hs);
	if (!hs)
		return;
	for (uint32_t i = 0; i < n0; i++)	/* (the candidates first: vectorizing adds blocks) */
		if (V->b[V->order[i]].npred == 2 && V->b[V->order[i]].term && V->p[V->b[V->order[i]].term].in.op == VO_BRIF)
			hs[nh++] = V->order[i];
	for (uint32_t i = 0; i < nh; i++) {
		uint32_t H = hs[i], t = V->b[H].term;
		bool found = false, done;

		for (int arm = 0; arm < 2 && !found; arm++) {
			uint32_t cur = arm ? V->p[t].in.c : V->p[t].in.b, prev = H, n = 1;

			S.lb[0] = H;
			while (cur != H && n < 64 && V->b[cur].npred == 1 && V->pr[V->b[cur].pred] == prev && V->b[cur].term &&
			       V->p[V->b[cur].term].in.op == VO_BR) {
				bool live = false;

				for (uint32_t p = V->b[cur].phead; p; p = V->p[p].next)
					live |= V->fwd[p] == p;
				if (live)
					break;
				S.lb[n++] = cur;
				prev = cur, cur = V->p[V->b[cur].term].in.a;
			}
			if (cur == H && n > 1) {
				found = true;
				S.nlb = n, S.L = prev, S.X = arm ? V->p[t].in.b : V->p[t].in.c;
			}
		}
		if (!found || S.X == H)
			continue;
		S.H = H;
		S.jL = vw_predix(V, H, S.L), S.jP = 1 - S.jL;
		if (S.jL > 1)
			continue;
		S.P = V->pr[V->b[H].pred + S.jP];
		if (S.P == S.L)
			continue;
		if (!S.I) {
			S.I = calloc((size_t)S.n0 + 1, sizeof *S.I);
			S.inl = calloc((size_t)V->nb + 1, 1);
			if (!S.I || !S.inl)
				break;
		}
		for (uint32_t k = 0; k < S.nlb; k++)
			S.inl[S.lb[k]] = 1;
		done = vv_loop(&S);
		if (done)
			vv_place(&S);
		if (vv_env() & 2) {
			const vident *id = &vintern_ents[e->c->syms[e->wk->fsym].name];

			snprintf(nm, sizeof nm, "%.*s", (int)id->len, id->s);
			fprintf(stderr, "vec: %s: loop b%u: %s%s\n", nm, H, done ? "vectorized" : "no: ", done ? "" : S.why ? S.why : "?");
		}
		for (uint32_t k = 0; k < S.nlb; k++)
			S.inl[S.lb[k]] = 0;
		for (uint32_t k = 0; k < S.ntouch; k++)
			S.I[S.touch[k]] = (vvi){ 0 };
		S.ntouch = 0;
	}
	free(hs), free(S.I), free(S.inl), free(S.touch);
	e->curloc = loc;
}
