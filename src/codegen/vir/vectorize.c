/*
 * The loop vectorizer (RESEARCH-vectors.md 6; SPEC-vir.md 7.5), codegen/vir/vectorize.c; it works
 * on the pre-IR, after if-conversion and before the edges are split, where blocks and
 * instructions can still be added.
 *
 * A loop it takes: innermost; the header H has two predecessors (P outside, the latch) and ends
 * in `brif icmp(u, n)` with u linear in the iteration and n invariant; the body is a chain of
 * blocks with one way in and one way out.  Every parameter of H is an induction (p + K each
 * iteration), a sum, a shifted value or an ordered reduction.  The instructions are pure, loads or stores; a load or store of 4
 * or 8 bytes has a constant element-aligned stride. Indexed reads require proven separation
 * from every written object; invariant reads must be unreachable by the loop's stores.
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
 * Nothing is reassociated: float reductions consume extracted lanes in scalar order (4.5).
 *
 * A value of the loop is one of: invariant (INV), linear in the iteration (LIN: an induction and
 * what add, sub, mul by a constant and ext make of it), different in each lane (VEC), a
 * comparison (MASK), a sum's parameter (RED) or its update (REDUPD).  `exact` says the linear
 * form holds as integers (an i32 operation without nsw may wrap): addresses and the trip count
 * need it, lanes do not.
 */

#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"
#include "vectorize.h"

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
	if (q->blk == VW_NOBLK && q->in.op == VIR_OP_ICONST) {
		x->k = q->in.type == VIR_TYPE_I32 ? (int64_t)(int32_t)q->in.b : (int64_t)((uint64_t)q->in.b | (uint64_t)q->in.c << 32);
	} else if (q->blk == VW_NOBLK && q->in.op == VIR_OP_SYMADDR) {
		x->objk = VJ_SYM, x->objv = q->in.a;
		x->k = (int64_t)((uint64_t)q->in.b | (uint64_t)q->in.c << 32);
	} else if (q->in.type == VIR_TYPE_PTR) {
		x->objk = VJ_PTR, x->objv = v;
	} else {
		x->bv = v, x->bs = 1;
	}
	return x;
}

static bool vv_isk(const vvi *x) { return x->kind == VK_INV && x->exact && !x->bv && !x->objk; }	/* a constant */
static bool vv_lin(const vvi *x) { return x->kind == VK_INV || x->kind == VK_LIN; }
static bool vv_lane(const vvi *x) { return x->kind == VK_INV || x->kind == VK_LIN || x->kind == VK_VEC || x->kind == VK_CARRY; }

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
	if (S->e->vw->p[q].in.type == VIR_TYPE_PTR) {
		r->objk = ok ? ok : VJ_PTR, r->objv = ok ? ov : q;
		if (!ok)
			r->bv = 0, r->bs = 0;
	}
}

VPRIV bool vv_skip(vwst *V, uint32_t q)
{
	return vw_find(V, q) != q || vw_promdead(V, q) || (V->p[q].in.op == VIR_OP_MEMSET && (V->p[q].in.flags & VW_LIFETIME));
}

/* instruction q of the loop: its kind.  false: the loop is not vectorized */
static bool vv_classify(vvs *S, uint32_t q)
{
	vwst *V = S->e->vw;
	vir_insn in = V->p[q].in;
	const vir_opcode_info *oi = &vir_opcode_table[in.op];
	uint32_t w[3] = { in.a, in.b, in.c };
	vvi *o[3] = { NULL, NULL, NULL }, r = { 0 };
	bool allinv = true;
	int no = 0;

	for (int k = 0; k < 3; k++)
		if (vw_isval(oi->operand_kinds[k])) {
			w[k] = vw_find(V, w[k]);
			o[k] = vv_get(S, w[k]);
			if (w[k] < S->n0 && o[k]->nuse < 0xffff)
				o[k]->nuse++;
			allinv &= o[k]->kind == VK_INV;
			no++;
		}
	for (uint32_t k = 0; k < no; k++) if (o[k]->kind == VK_FOLD) {
		if (!(vir_opcode_table[in.op].effects & VIR_EFFECT_P)) return vv_no(S, "an ordered reduction used by an effect");
		r.kind = VK_FOLD;
		goto done;
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
	case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_PTRADD:
		if (vv_lin(o[0]) && vv_lin(o[1])) {
			bool rep = vv_comb(&r, o[0], o[1], in.op == VIR_OP_SUB ? -1 : 1);
			bool ex = rep && o[0]->exact && o[1]->exact && (in.type != VIR_TYPE_I32 || ((in.flags & VIR_INSN_FLAG_NSW) && !o[0]->uns && !o[1]->uns));

			r.kind = allinv ? VK_INV : VK_LIN, r.exact = ex;
			if (allinv && !ex)
				vv_opaque(S, q, &r);
			else if (!ex && in.type != VIR_TYPE_I32)
				r.kind = VK_BAD;
		} else {
			r.kind = in.type == S->ity && vv_lane(o[0]) && vv_lane(o[1]) ? VK_VEC : VK_BAD;
		}
		break;
	case VIR_OP_MUL: case VIR_OP_SHL: {
		int kc = vv_isk(o[1]) ? 1 : in.op == VIR_OP_MUL && vv_isk(o[0]) ? 0 : -1;

		if (kc >= 0 && vv_lin(o[1 - kc]) && !o[1 - kc]->objk && (in.op == VIR_OP_MUL || (o[1]->k >= 0 && o[1]->k < (in.type == VIR_TYPE_I64 ? 63 : 31)))) {	/* (an i32 shift has no nsw: linear lanes, no exact form) */
			const vvi *x = o[1 - kc];
			uint64_t K = in.op == VIR_OP_MUL ? (uint64_t)o[kc]->k : 1ull << o[1]->k;

			r = (vvi){ .kind = allinv ? VK_INV : VK_LIN, .c = (int64_t)((uint64_t)x->c * K), .bv = x->bv,
				   .bs = (int64_t)((uint64_t)x->bs * K), .k = (int64_t)((uint64_t)x->k * K) };
			r.exact = x->exact && (in.type != VIR_TYPE_I32 || ((in.flags & VIR_INSN_FLAG_NSW) && !x->uns));
			if (!r.bs)
				r.bv = 0;
			if (allinv && !r.exact)
				vv_opaque(S, q, &r);
			else if (!r.exact && in.type != VIR_TYPE_I32)
				r.kind = VK_BAD;
		} else if (allinv) {
			vv_opaque(S, q, &r);
		} else {
			r.kind = in.op == VIR_OP_MUL && in.type == VIR_TYPE_I32 && S->ity == VIR_TYPE_I32 && vv_lane(o[0]) && vv_lane(o[1]) ? VK_VEC : VK_BAD;	/* (no multiply on 64-bit lanes) */
		}
		break;
	}
	case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR: case VIR_OP_NEG: case VIR_OP_NOT:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = in.type == S->ity && vv_lane(o[0]) && (no < 2 || vv_lane(o[1])) ? VK_VEC : VK_BAD;
		break;
	case VIR_OP_EXT:
		if (VIR_INSN_FLAG_WIDTH_LOG2(in.flags) == 2 && in.type == VIR_TYPE_I64 && V->p[w[0]].in.type == VIR_TYPE_I32 && vv_lin(o[0]) && o[0]->exact &&
		    ((in.flags & VIR_INSN_FLAG_MS) ? !o[0]->uns : o[0]->uns)) {	/* (the extension that keeps its form's value) */
			r = *o[0];
			r.nuse = 0, r.sce = r.scb = r.sc0 = r.vec = 0, r.uns = 0;
		} else if (allinv) {
			vv_opaque(S, q, &r);
		} else {	/* (the walker selects int values as i64 and truncates) */
			r.kind = VIR_INSN_FLAG_WIDTH_LOG2(in.flags) == 2 && in.type == VIR_TYPE_I64 && V->p[w[0]].in.type == VIR_TYPE_I32 && S->ity == VIR_TYPE_I32 && vv_lane(o[0]) ? VK_W32 : VK_BAD;
		}
		break;
	case VIR_OP_I2P:	/* an address computed as an integer: its invariant term is the object */
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
	case VIR_OP_TRUNC:
		if (allinv)
			vv_opaque(S, q, &r);
		else if (o[0]->kind == VK_LIN)
			r = (vvi){ .kind = VK_LIN, .c = (int32_t)o[0]->c };
		else
			r.kind = o[0]->kind == VK_W32 ? VK_VEC : VK_BAD;
		break;
	case VIR_OP_ICMP:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = V->p[w[0]].in.type == S->ity && vv_lane(o[0]) && vv_lane(o[1]) ? VK_MASK : VK_BAD;
		break;
	case VIR_OP_FCMP:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = V->p[w[0]].in.type == S->fty && vv_lane(o[0]) && vv_lane(o[1]) ? VK_MASK : VK_BAD;
		break;
	case VIR_OP_SELECT:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = o[0]->kind == VK_MASK && (in.type == S->ity || in.type == S->fty) && vv_lane(o[1]) && vv_lane(o[2]) ? VK_VEC :
				 o[0]->kind == VK_MASK && in.type == VIR_TYPE_I64 && S->ity == VIR_TYPE_I32 && (o[1]->kind == VK_W32 || vv_isk(o[1])) &&
				 (o[2]->kind == VK_W32 || vv_isk(o[2])) ? VK_W32 : VK_BAD;
		break;
	case VIR_OP_FADD: case VIR_OP_FSUB: case VIR_OP_FMUL: case VIR_OP_FDIV: case VIR_OP_FNEG: case VIR_OP_FABS: case VIR_OP_FSQRT:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = in.type == S->fty && vv_lane(o[0]) && (no < 2 || vv_lane(o[1])) ? VK_VEC : VK_BAD;
		break;
	case VIR_OP_FMA:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = in.type == S->fty && vv_lane(o[0]) && vv_lane(o[1]) && vv_lane(o[2]) ? VK_VEC : VK_BAD;
		break;
	case VIR_OP_S2F:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = in.type == S->fty && V->p[w[0]].in.type == S->ity && vv_lane(o[0]) ? VK_VEC :
				 /* (double)i: the i32 index widened lane by lane */
				 in.type == VIR_TYPE_F64 && S->fty == VIR_TYPE_F64 && V->p[w[0]].in.type == VIR_TYPE_I32 && o[0]->kind == VK_LIN && o[0]->exact && !o[0]->uns ? VK_VEC : VK_BAD;
		break;
	case VIR_OP_SDIV: case VIR_OP_UDIV: case VIR_OP_SREM: case VIR_OP_UREM: case VIR_OP_LSHR: case VIR_OP_ASHR: case VIR_OP_UMULHI: case VIR_OP_SMULHI:
	case VIR_OP_CLZ: case VIR_OP_CTZ: case VIR_OP_POPCNT: case VIR_OP_BSWAP: case VIR_OP_U2F: case VIR_OP_F2S: case VIR_OP_F2U: case VIR_OP_FEXT:
	case VIR_OP_FTRUNC: case VIR_OP_BITCAST: case VIR_OP_PSUB: case VIR_OP_P2I:
		if (allinv)
			vv_opaque(S, q, &r);
		else
			r.kind = VK_BAD;
		break;
	case VIR_OP_LOAD:
		if (in.flags & VIR_INSN_FLAG_VOL)
			return vv_no(S, "a volatile load");
		if (!vv_lin(o[0]) || !o[0]->exact || !o[0]->objk) {
			r.kind = VK_BAD;
		} else if (o[0]->kind == VK_INV) {
			if (S->ninvld == 64)
				return vv_no(S, "too many invariant loads");
			S->invld[S->ninvld++] = q;
			vv_opaque(S, q, &r);
		} else if (S->lg == 2 && VIR_INSN_FLAG_WIDTH_LOG2(in.flags) == 2 && in.type == VIR_TYPE_I64 &&
		           (o[0]->c == 4 || o[0]->c == -4)) {
			if (S->nacc == 64) return vv_no(S, "too many accesses");
			S->acc[S->nacc++] = (vvacc){q, S->pos, false};
			r.kind = VK_W32;
		} else if (VIR_INSN_FLAG_WIDTH_LOG2(in.flags) == S->lg && (in.type == S->fty || in.type == S->ity) && (o[0]->c && o[0]->c % S->esz == 0 && o[0]->c >= -(1 << 20) && o[0]->c <= (1 << 20))) {
			if (S->nacc == 64)
				return vv_no(S, "too many accesses");
			S->acc[S->nacc++] = (vvacc){ q, S->pos, false };
			r.kind = VK_VEC;
		} else {
			r.kind = VK_BAD;
		}
		break;
	case VIR_OP_STORE: {
		uint32_t vt = V->p[w[1]].in.type;

		if (in.flags & VIR_INSN_FLAG_VOL)
			return vv_no(S, "a volatile store");
		if (!vv_lin(o[0]) || !o[0]->exact || !o[0]->objk)
			return vv_no(S, "a store whose address is not base + linear");
		if (!o[0]->c || o[0]->c % S->esz || o[0]->c < -(1 << 20) || o[0]->c > (1 << 20))
			return vv_no(S, o[0]->c ? "a store with a stride other than its size" : "a store to one address");
		if (VIR_INSN_FLAG_WIDTH_LOG2(in.flags) != S->lg || (vt != S->fty && vt != S->ity))
			return vv_no(S, "a store of another width than the loop's lanes");
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
	/* Expand pure address arithmetic lane by lane when it has no packed form. */
	if (r.kind == VK_BAD && (in.op == VIR_OP_ADD || in.op == VIR_OP_SUB || in.op == VIR_OP_MUL || in.op == VIR_OP_SHL ||
	    in.op == VIR_OP_LSHR || in.op == VIR_OP_ASHR || in.op == VIR_OP_EXT || in.op == VIR_OP_TRUNC || in.op == VIR_OP_I2P ||
	    in.op == VIR_OP_P2I || in.op == VIR_OP_PTRADD || in.op == VIR_OP_AND || in.op == VIR_OP_OR || in.op == VIR_OP_XOR)) {
		bool ok = true;
		for (uint32_t k = 0; k < no; k++)
			ok &= vv_lane(o[k]) || o[k]->kind == VK_W32 || o[k]->kind == VK_SCALAR;
		if (ok) {
			r.kind = VK_SCALAR;
			if (in.op == VIR_OP_ADD || in.op == VIR_OP_PTRADD || in.op == VIR_OP_SUB) {
				if (o[0]->objk && !o[1]->objk) r.objk = o[0]->objk, r.objv = o[0]->objv;
				else if (in.op != VIR_OP_SUB && o[1]->objk && !o[0]->objk) r.objk = o[1]->objk, r.objv = o[1]->objv;
			} else if (in.op == VIR_OP_I2P || in.op == VIR_OP_P2I)
				r.objk = o[0]->objk, r.objv = o[0]->objv;
		}
	}
	if (in.op == VIR_OP_LOAD && r.kind == VK_BAD && o[0]->kind == VK_SCALAR && o[0]->objk &&
	    VIR_INSN_FLAG_WIDTH_LOG2(in.flags) == S->lg && (in.type == S->fty || in.type == S->ity)) {
		if (S->ngather == 64) return vv_no(S, "too many indexed reads");
		S->gather[S->ngather++] = q;
		r.kind = VK_VEC;
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

	/* An indexed read is safe only when every written object is a distinct symbol. */
	for (uint32_t i = 0; i < S->ngather; i++) {
		const vvi *a = vv_get(S, vw_find(V, V->p[S->gather[i]].in.a));
		for (uint32_t j = 0; j < S->nacc; j++) {
			const vvi *b = vv_get(S, vw_find(V, V->p[S->acc[j].q].in.a));
			if (S->acc[j].store && !(a->objk == VJ_SYM && b->objk == VJ_SYM && a->objv != b->objv))
				return vv_no(S, "an indexed read that may alias a store");
		}
	}
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
			if ((s->k - a->k) % (int64_t)S->esz)
				return vv_no(S, "two accesses of one array that overlap in part");
			if ((s->k - a->k) % s->c)
				continue; /* aligned elements in different residue classes of the stride */
			d = (s->k - a->k) / s->c;	/* a touches the store's element d iterations later */
			g = d < 0 ? -d : d;
			if (!d || g >= (int64_t)S->vf)
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

static uint32_t vv_scalar(vvs *S, uint32_t v, int mode);

/* a copy of pure instruction or load v in block b, its operands through vv_scalar */
static uint32_t vv_clone(vvs *S, uint32_t v, uint32_t b, int mode)
{
	vwst *V = S->e->vw;
	vir_insn in = V->p[v].in;
	const vir_opcode_info *oi = &vir_opcode_table[in.op];
	uint32_t w[3] = { in.a, in.b, in.c };

	for (int k = 0; k < 3; k++)
		if (vw_isval(oi->operand_kinds[k]) && !(w[k] = vv_scalar(S, w[k], mode)))
			return 0;
	return vv_ins(S, b, in.op, in.type, in.flags, w[0], w[1], w[2]);
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
	if (V->p[v].in.op == VIR_OP_BPARAM) {	/* an induction */
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

/* the vector of the loop's lanes for scalar type ty; 0 when ty is not a lane type of this loop */
static uint32_t vv_vty(const vvs *S, uint32_t ty)
{
	if (ty != S->ity && ty != S->fty)
		return 0;
	return ty == VIR_TYPE_F32 ? VIR_TYPE_F32X4 : ty == VIR_TYPE_I32 ? VIR_TYPE_I32X4 : ty == VIR_TYPE_F64 ? VIR_TYPE_F64X2 : VIR_TYPE_I64X2;
}

/* lanes 0, c, 2c, 3c */
static uint32_t vv_step(vvs *S, int64_t c)
{
	uint32_t s, k;

	for (uint32_t i = 0; i < S->nstep; i++)
		if (S->stepc[i] == c)
			return S->stepv[i];
	s = vv_ins(S, S->VPH, VIR_OP_VSERIES, vv_vty(S, S->ity), 0, 0, 0, 0);
	if (S->ity == VIR_TYPE_I64 && c != 1) {	/* (two lanes, no multiply: 0 and c are c & (0, -1)) */
		k = vv_ins(S, S->VPH, VIR_OP_VSPLAT, VIR_TYPE_I64X2, 0, vw_iconst(S->e, VIR_TYPE_I64, c), 0, 0);
		s = vv_ins(S, S->VPH, VIR_OP_AND, VIR_TYPE_I64X2, 0, k, vv_ins(S, S->VPH, VIR_OP_NEG, VIR_TYPE_I64X2, 0, s, 0, 0), 0);
	} else if ((int32_t)c != 1) {
		k = vv_ins(S, S->VPH, VIR_OP_VSPLAT, VIR_TYPE_I32X4, 0, vw_iconst(S->e, VIR_TYPE_I32, (int32_t)c), 0, 0);
		s = vv_ins(S, S->VPH, VIR_OP_MUL, VIR_TYPE_I32X4, 0, s, k, 0);
	}
	if (S->nstep < 4)
		S->stepc[S->nstep] = c, S->stepv[S->nstep++] = s;
	return s;
}

static uint32_t vv_addr(vvs *S, uint32_t q, bool *rev);
static uint32_t vv_vec(vvs *S, uint32_t v);
static uint32_t vv_mask(vvs *S, uint32_t v);

static uint32_t vv_mask(vvs *S, uint32_t v)
{
	vwst *V = S->e->vw;
	vir_insn in;
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
	r = vv_ins(S, S->VB, in.op == VIR_OP_ICMP ? VIR_OP_VICMP : VIR_OP_VFCMP, vv_vty(S, S->ity), in.flags & 15, a, b, 0);
	S->I[v].vec = r;
	return r;
}

/* the i32 lanes of a widened value (VK_W32, or a constant): what its trunc is */
static uint32_t vv_w32(vvs *S, uint32_t v)
{
	vwst *V = S->e->vw;
	vvi *x;
	vir_insn in;
	uint32_t a, b, c, r;

	v = vw_find(V, v);
	x = vv_get(S, v);
	if (v < S->n0 && x->vec)
		return x->vec;
	in = V->p[v].in;
	if (vv_isk(x)) {
		r = vv_ins(S, S->VPH, VIR_OP_VSPLAT, VIR_TYPE_I32X4, 0, vw_iconst(S->e, VIR_TYPE_I32, (int32_t)x->k), 0, 0);
	} else if (x->kind == VK_W32 && in.op == VIR_OP_EXT) {
		return vv_vec(S, in.a);
	} else if (x->kind == VK_W32 && in.op == VIR_OP_SELECT) {
		if (!(a = vv_mask(S, in.a)) || !(b = vv_w32(S, in.b)) || !(c = vv_w32(S, in.c)))
			return 0;
		r = vv_ins(S, S->VB, VIR_OP_VBITSEL, VIR_TYPE_I32X4, 0, a, b, c);
	} else {
		return vv_no(S, "a value that cannot be vectorized"), 0;
	}
	if (v < S->n0)
		S->I[v].vec = r;
	return r;
}

/* Reconstruct a scalar address or integer expression for one active lane. */
static uint32_t vv_at(vvs *S, uint32_t v, uint32_t lane, uint32_t *budget)
{
	if (!*budget) return vv_no(S, "an indexed expression beyond the budget"), 0;
	--*budget;
	vwst *V = S->e->vw;
	v = vw_find(V, v);
	vvi *x = vv_get(S, v);
	vir_insn in = V->p[v].in;
	if (x->kind == VK_INV) return vv_scalar(S, v, VM_BODY);
	if (x->kind == VK_LIN) {
		uint32_t a = vv_scalar(S, v, VM_BODY);
		if (!a || !lane) return a;
		return vv_ins(S, S->VB, in.type == VIR_TYPE_PTR ? VIR_OP_PTRADD : VIR_OP_ADD, in.type, 0, a,
			vw_iconst(S->e, in.type == VIR_TYPE_I32 ? VIR_TYPE_I32 : VIR_TYPE_I64, lane * x->c), 0);
	}
	if (x->kind == VK_MASK) {
		uint32_t a=vv_mask(S,v);
		if(!a)return 0;
		uint32_t r=vv_ins(S,S->VB,VIR_OP_VEXTRACT,S->ity,0,a,lane,0);
		return S->ity==VIR_TYPE_I32?r:vv_ins(S,S->VB,VIR_OP_TRUNC,VIR_TYPE_I32,0,r,0,0);
	}
	if (x->kind == VK_VEC || x->kind == VK_CARRY) {
		uint32_t a = vv_vec(S, v);
		return a ? vv_ins(S, S->VB, VIR_OP_VEXTRACT, in.type, 0, a, lane, 0) : 0;
	}
	if (x->kind == VK_W32 && in.op == VIR_OP_LOAD) {
		if (!x->vec) return vv_no(S, "an indexed operand used before its load"), 0;
		uint32_t a = vv_ins(S, S->VB, VIR_OP_VEXTRACT, VIR_TYPE_I32, 0, x->vec, lane, 0);
		return vv_ins(S, S->VB, VIR_OP_EXT, VIR_TYPE_I64, 2 | (in.flags & VIR_INSN_FLAG_MS), a, 0, 0);
	}
	if (x->kind == VK_SCALAR || x->kind == VK_W32) {
		uint32_t w[3] = {in.a, in.b, in.c};
		for (int k = 0; k < 3; k++)
			if (vw_isval(vir_opcode_table[in.op].operand_kinds[k]) && !(w[k] = vv_at(S, w[k], lane, budget))) return 0;
		return vv_ins(S, S->VB, in.op, in.type, in.flags, w[0], w[1], w[2]);
	}
	return vv_no(S, "an address operand that cannot be expanded"), 0;
}

/* Clone only the recurrence expression; independent work stays packed. */
static uint32_t vv_foldat(vvs *S, uint32_t v, uint32_t lane, uint32_t *budget)
{
	vwst *V = S->e->vw;
	v = vw_find(V, v);
	for(uint32_t i=0;i<S->nfold;i++)if(v==S->fold[i].p)return S->fold[i].cur;
	if (vv_get(S, v)->kind != VK_FOLD) return vv_at(S, v, lane, budget);
	if (!*budget || V->p[v].in.op == VIR_OP_BPARAM) return vv_no(S, "an ordered reduction expression beyond the budget"), 0;
	--*budget;
	vir_insn in = V->p[v].in;
	uint32_t w[3] = {in.a,in.b,in.c};
	for (int k=0;k<3;k++) if (vw_isval(vir_opcode_table[in.op].operand_kinds[k]))
		if (!(w[k] = vv_foldat(S,w[k],lane,budget))) return 0;
	return vv_ins(S,S->VB,in.op,in.type,in.flags,w[0],w[1],w[2]);
}

/* v's lanes */
static uint32_t vv_vec(vvs *S, uint32_t v)
{
	vwst *V = S->e->vw;
	vvi *x;
	vir_insn in;
	uint32_t ty, vt, r = 0, a, b, c;

	v = vw_find(V, v);
	x = vv_get(S, v);
	if (v < S->n0 && x->vec)
		return x->vec;
	ty = V->p[v].in.type, vt = vv_vty(S, ty);
	if (!vt || v >= S->n0) {
		vv_no(S, "a value of another width than the loop's lanes");
		return 0;
	}
	in = V->p[v].in;
	switch (x->kind) {
	case VK_INV:
		if (!(a = vv_scalar(S, v, VM_BODY)))
			return 0;
		r = vv_ins(S, S->VPH, VIR_OP_VSPLAT, vt, 0, a, 0, 0);
		break;
	case VK_LIN: {
		int64_t cc = x->c;

		if (ty != S->ity || !(a = vv_scalar(S, v, VM_BODY)))
			return vv_no(S, "a linear value of another width than the lanes"), 0;
		a = vv_ins(S, S->VB, VIR_OP_VSPLAT, vt, 0, a, 0, 0);
		r = vv_ins(S, S->VB, VIR_OP_ADD, vt, 0, a, vv_step(S, cc), 0);
		break;
	}
	case VK_CARRY:
		for (uint32_t i = 0; i < S->ncarry; i++) if (S->carry[i].p == v) {
			if (S->carry[i].busy) return vv_no(S, "a cyclic carried expression"), 0;
			S->carry[i].busy = true;
			if (!(a = vv_vec(S, S->carry[i].next))) return 0;
			r = vv_ins(S, S->VB, VIR_OP_VSPLAT, vt, 0, S->carry[i].par, 0, 0);
			for (uint32_t lane = 1; lane < S->vf; lane++) {
				b = vv_ins(S, S->VB, VIR_OP_VEXTRACT, ty, 0, a, lane-1, 0);
				r = vv_ins(S, S->VB, VIR_OP_VINSERT, vt, 0, r, b, lane);
			}
			S->carry[i].last = vv_ins(S, S->VB, VIR_OP_VEXTRACT, ty, 0, a, S->vf-1, 0);
			S->carry[i].busy = false;
			break;
		}
		break;
	case VK_VEC:
		switch (in.op) {
		case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_MUL: case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR:
		case VIR_OP_FADD: case VIR_OP_FSUB: case VIR_OP_FMUL: case VIR_OP_FDIV:
			if (!(a = vv_vec(S, in.a)) || !(b = vv_vec(S, in.b)))
				return 0;
			r = vv_ins(S, S->VB, in.op, vt, 0, a, b, 0);
			break;
		case VIR_OP_FMA: {
			uint32_t c;

			if (!(a = vv_vec(S, in.a)) || !(b = vv_vec(S, in.b)) || !(c = vv_vec(S, in.c)))
				return 0;
			r = vv_ins(S, S->VB, VIR_OP_FMA, vt, in.flags, a, b, c);
			break;
		}
		case VIR_OP_NEG: case VIR_OP_NOT: case VIR_OP_FNEG: case VIR_OP_FABS: case VIR_OP_FSQRT:
			if (!(a = vv_vec(S, in.a)))
				return 0;
			r = vv_ins(S, S->VB, in.op, vt, 0, a, 0, 0);
			break;
		case VIR_OP_TRUNC:
			if (!(r = vv_w32(S, in.a)))
				return 0;
			break;
		case VIR_OP_S2F:
			if (V->p[vw_find(V, in.a)].in.type != S->ity) {	/* (an exact i32 index into 64-bit lanes) */
				int64_t cc = vv_get(S, vw_find(V, in.a))->c;

				if (!(a = vv_scalar(S, in.a, VM_BODY)))
					return 0;
				a = vv_ins(S, S->VB, VIR_OP_EXT, VIR_TYPE_I64, 2 | VIR_INSN_FLAG_MS, a, 0, 0);
				a = vv_ins(S, S->VB, VIR_OP_VSPLAT, VIR_TYPE_I64X2, 0, a, 0, 0);
				a = vv_ins(S, S->VB, VIR_OP_ADD, VIR_TYPE_I64X2, 0, a, vv_step(S, cc), 0);
			} else if (!(a = vv_vec(S, in.a)))
				return 0;
			r = vv_ins(S, S->VB, VIR_OP_VS2F, vt, 0, a, 0, 0);
			break;
		case VIR_OP_SELECT:
			if (!(a = vv_mask(S, in.a)) || !(b = vv_vec(S, in.b)) || !(c = vv_vec(S, in.c)))
				return 0;
			r = vv_ins(S, S->VB, VIR_OP_VBITSEL, vt, 0, a, b, c);
			break;
		case VIR_OP_LOAD: {
			/* A recurrence can need a later load. Move it only past stores to other symbols. */
			const vvi *addr = vv_get(S, vw_find(V,in.a));
			uint32_t pos = UINT32_MAX;
			for (uint32_t k=0;k<S->nacc;k++) if (S->acc[k].q == v) pos = S->acc[k].pos;
			if (pos == UINT32_MAX || (addr->c != (int64_t)S->esz && addr->c != -(int64_t)S->esz))
				return vv_no(S,"an indexed recurrence load"),0;
			for (uint32_t k=0;k<S->nacc;k++) if (S->acc[k].store && S->acc[k].pos < pos) {
				const vvi *st = vv_get(S,vw_find(V,V->p[S->acc[k].q].in.a));
				if (!(addr->objk == VJ_SYM && st->objk == VJ_SYM && addr->objv != st->objv))
					return vv_no(S,"a recurrence load that may cross a store"),0;
			}
			bool rev;
			if (!(a=vv_addr(S,v,&rev))) return 0;
			r=vv_ins(S,S->VB,VIR_OP_LOAD,vt,in.flags & ~(uint32_t)VIR_INSN_FLAG_MS,a,in.b,0);
			if (rev) r=vv_ins(S,S->VB,VIR_OP_VREV,vt,0,r,0,0);
			break;
		}
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
 * reversed: a pointer of the vector body that steps vf * stride (contiguous reversed accesses use the lowest address) */
static uint32_t vv_addr(vvs *S, uint32_t q, bool *rev)
{
	vwst *V = S->e->vw;
	uint32_t a = vw_find(V, V->p[q].in.a), s;

	*rev = vv_get(S, a)->c == -(int64_t)S->esz;
	for (uint32_t i = 0; i < S->npin; i++)
		if (S->pin[i].a == a)
			return S->pin[i].par;
	if (S->npin == 32)
		return vv_no(S, "too many addresses"), 0;
	if (!(s = vv_scalar(S, a, VM_T0)))
		return 0;
	if (*rev)
		s = vv_ins(S, S->VPH, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, s, vw_iconst(S->e, VIR_TYPE_I64, -(int64_t)(16 - S->esz)), 0);
	S->pin[S->npin].a = a, S->pin[S->npin].rev = *rev;
	return S->pin[S->npin++].par = vv_param(S, S->VB, VIR_TYPE_PTR, s, 0, 0);
}

/* [lo, hi): the bytes access x (vv_chk's index) touches in the vector iterations; span = 4 * nv */
static bool vv_range(vvs *S, uint32_t x, uint32_t span, uint32_t *lo, uint32_t *hi)
{
	vwst *V = S->e->vw;
	uint32_t q = x >= 64 ? S->invld[x - 64] : S->acc[x].q, a = vw_find(V, V->p[q].in.a), s = vv_scalar(S, a, VM_T0);

	if (!s)
		return false;
	int64_t stride = vv_get(S, a)->c, mag = stride < 0 ? -stride : stride;
	if (x < 64 && mag != S->esz) {
		/* Cover the first through last touched element, including gaps. */
		span = vv_ins(S, S->VPH, VIR_OP_LSHR, VIR_TYPE_I64, 0, span, vw_iconst(S->e, VIR_TYPE_I64, S->lg), 0);
		span = vv_ins(S, S->VPH, VIR_OP_SUB, VIR_TYPE_I64, 0, span, vw_iconst(S->e, VIR_TYPE_I64, 1), 0);
		span = vv_ins(S, S->VPH, VIR_OP_MUL, VIR_TYPE_I64, 0, span, vw_iconst(S->e, VIR_TYPE_I64, mag), 0);
		span = vv_ins(S, S->VPH, VIR_OP_ADD, VIR_TYPE_I64, 0, span, vw_iconst(S->e, VIR_TYPE_I64, S->esz), 0);
	}
	if (x >= 64) {
		*lo = s, *hi = vv_ins(S, S->VPH, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, s, vw_iconst(S->e, VIR_TYPE_I64, 1 << VIR_INSN_FLAG_WIDTH_LOG2(V->p[q].in.flags)), 0);
	} else if (vv_get(S, a)->c > 0) {
		*lo = s, *hi = vv_ins(S, S->VPH, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, s, span, 0);
	} else {
		*hi = vv_ins(S, S->VPH, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, s, vw_iconst(S->e, VIR_TYPE_I64, S->esz), 0);
		*lo = vv_ins(S, S->VPH, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, *hi, vv_ins(S, S->VPH, VIR_OP_NEG, VIR_TYPE_I64, 0, span, 0, 0), 0);
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
	uint32_t t = vw_put(S->e, op, VIR_TYPE_VOID, 0, x, y, z, b);

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
		vir_insn N;
		uint32_t a, b;
		int64_t k;

		if (n == 8 || v >= S->n0 || V->p[v].blk == VW_NOBLK || !S->inl[V->p[v].blk])
			goto no;
		N = V->p[v].in;
		if (N.op != VIR_OP_ADD && N.op != VIR_OP_SUB && N.op != VIR_OP_PTRADD)
			goto no;
		a = vw_find(V, N.a), b = vw_find(V, N.b);
		if (vw_kint(V, b, &k)) {
			*K += N.op == VIR_OP_SUB ? -k : k;
		} else if (N.op == VIR_OP_ADD && b < S->n0 && V->p[b].in.op != VIR_OP_ICONST && (V->p[b].blk == VW_NOBLK || !S->inl[V->p[b].blk]) && S->none < 8) {
			S->one[S->none++] = b;
			*K += 1;
		} else {
			goto no;
		}
		if (N.type == VIR_TYPE_I32 && !(N.flags & VIR_INSN_FLAG_NSW))
			*nsw = false;
		v = a;
	}
	if (*K)
		return true;
no:
	S->none = none0;
	return false;
}

/* the loop goes on while parameter p is below an invariant, unsigned, or not equal to it: the
 * header's test (stepping 1 from below, p reaches the bound before it could wrap; the vector loop
 * is entered only when it starts below: the span's test) */
static bool vv_ultctl(vvs *S, uint32_t p)
{
	vwst *V = S->e->vw;
	uint32_t t = V->b[S->H].term, c = vw_find(V, V->p[t].in.a), cc;

	if (c >= S->n0 || V->p[c].in.op != VIR_OP_ICMP || V->p[c].blk != S->H)
		return false;
	cc = V->p[c].in.flags & 15;
	if (V->p[t].in.b == S->X)
		cc = (uint32_t[]){ 1, 0, 5, 4, 3, 2, 9, 8, 7, 6 }[cc];
	return ((cc == 6 || cc == 1) && vw_find(V, V->p[c].in.a) == p) || ((cc == 8 || cc == 1) && vw_find(V, V->p[c].in.b) == p);
}

/* the loop headed by H (S->lb, P, L, X set): analysed and, when it can be, vectorized */
/* Does the update depend on this carried parameter, without following another phi? */
static bool vv_reaches(vwst *V, uint32_t v, uint32_t p, uint32_t *budget)
{
	v = vw_find(V,v);
	if (v == p) return true;
	if (!*budget) return true;
	--*budget;
	vir_insn in = V->p[v].in;
	if (V->p[v].blk == VW_NOBLK || in.op == VIR_OP_BPARAM) return false;
	uint32_t w[3] = {in.a,in.b,in.c};
	for (int k=0;k<3;k++) if (vw_isval(vir_opcode_table[in.op].operand_kinds[k]) && vv_reaches(V,w[k],p,budget)) return true;
	return false;
}

static bool vv_loop(vvs *S)
{
	ecx *e = S->e;
	vwst *V = e->vw;
	uint32_t H = S->H, t = V->b[H].term, cond = vw_find(V, V->p[t].in.a), u, n, cc, d, cnt, cntn, nv, guard, z = 0;
	bool incl;
	int64_t c, ac;
	vvi *ui, *ni;
	uint32_t jp[40], njp = 0;	/* J's parameter per live parameter of H, in H's order */

	S->nfold = S->ncarry = S->ngather = S->nacc = S->ninvld = S->nred = S->nind = S->nstep = S->nchk = S->npin = S->none = 0, S->pos = 0, S->why = NULL;
	/* the lanes: the width of the loop's first store (of its first load when it stores nothing) */
	S->lg = 0;
	for (int st = 1; st >= 0 && !S->lg; st--)
		for (uint32_t i = 0; i < S->nlb && !S->lg; i++)
			for (uint32_t q = V->b[S->lb[i]].head; q && !S->lg; q = V->p[q].next)
				if (!vv_skip(V, q) && V->p[q].in.op == (st ? VIR_OP_STORE : VIR_OP_LOAD)) {
					uint32_t w = VIR_INSN_FLAG_WIDTH_LOG2(V->p[q].in.flags);

					if (st && w != 2 && w != 3)
						return vv_no(S, "a store that is not 4 or 8 bytes");
					if (w == 2 || w == 3)
						S->lg = w;
				}
	if (!S->lg)
		S->lg = 2;
	S->esz = 1u << S->lg, S->vf = 16 / S->esz;
	S->ity = S->lg == 2 ? VIR_TYPE_I32 : VIR_TYPE_I64, S->fty = S->lg == 2 ? VIR_TYPE_F32 : VIR_TYPE_F64;
	/* H's parameters */
	for (uint32_t p = V->b[H].phead; p; p = V->p[p].next) {
		uint32_t init, nx, ty = V->p[p].in.type, budget = 256;
		vir_insn N;
		vvi *x, r = { 0 };
		int64_t K = 0;
		bool kb;

		if (V->fwd[p] != p)
			continue;
		init = vw_find(V, V->x[V->p[p].in.b + S->jP]), nx = vw_find(V, V->x[V->p[p].in.b + S->jL]);
		if (init >= S->n0 || nx >= S->n0 || V->p[nx].blk == VW_NOBLK || !S->inl[V->p[nx].blk])
			return vv_no(S, "a value carried around the loop that is neither an induction nor a sum");
		if (V->p[init].blk != VW_NOBLK && S->inl[V->p[init].blk])
			return vv_no(S, "an initial value from inside the loop");
		N = V->p[nx].in;
		if (vv_chain(S, p, nx, &K, &kb)) {
			vvi *ii = vv_get(S, init), kk = { .kind = VK_INV, .exact = 1, .c = K };
			bool rep = vv_comb(&r, ii, &kk, 1);

			if (S->nind == 32)
				return vv_no(S, "too many inductions");
			r.kind = VK_LIN, r.exact = rep && ii->exact && (ty != VIR_TYPE_I32 || kb);
			if (!r.exact && rep && ii->exact && K == 1 && vv_ultctl(S, p) && (!vv_isk(ii) || ii->k >= 0))
				r.exact = r.uns = 1;	/* (below an unsigned bound, stepping 1: it cannot wrap) */
			if (!r.exact && ty != VIR_TYPE_I32)
				return vv_no(S, "an induction without a linear form");
			S->indk[S->nind] = K;
			S->ind[S->nind++] = p;
		} else if ((ty == S->fty && ((N.op == VIR_OP_FADD || N.op == VIR_OP_FMUL) && (vw_find(V, N.a) == p || vw_find(V, N.b) == p) || (N.op == VIR_OP_FSUB || N.op == VIR_OP_FDIV) && vw_find(V, N.a) == p)) ||
			   (ty == S->ity && ((N.op == VIR_OP_ADD && (vw_find(V, N.a) == p || vw_find(V, N.b) == p)) || (N.op == VIR_OP_SUB && vw_find(V, N.a) == p)))) {
			if (S->nred == 8)
				return vv_no(S, "too many sums");
			S->red[S->nred++] = (vvred){ p, nx, vw_find(V, N.a) == p ? N.b : N.a, 0, 0 };
			r.kind = VK_RED;
		} else if ((ty == S->fty || (ty == S->ity && N.op == VIR_OP_SELECT)) && S->nfold < 8 && vv_reaches(V, nx, p, &budget) && (N.op == VIR_OP_SELECT || N.op == VIR_OP_FADD || N.op == VIR_OP_FSUB || N.op == VIR_OP_FMUL || N.op == VIR_OP_FDIV)) {
			S->fold[S->nfold].p = p;
			S->fold[S->nfold++].next = nx;
			r.kind = VK_FOLD;
		} else if ((ty == S->fty || ty == S->ity) && S->ncarry < 8 && (N.op == VIR_OP_LOAD || N.op == VIR_OP_FMUL || N.op == VIR_OP_FADD || N.op == VIR_OP_FSUB || N.op == VIR_OP_BPARAM)) {
			S->carry[S->ncarry].p = p;
			S->carry[S->ncarry].next = nx;
			S->carry[S->ncarry++].busy = false;
			r.kind = VK_CARRY;
		} else {
			return vv_no(S, ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64 ? "a float carried around the loop (a reduction or a recurrence: order matters)" :
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
		if (V->p[tt].in.op == VIR_OP_BRIF) {
			vvi *x = vv_get(S, vw_find(V, V->p[tt].in.a));

			if (x->nuse < 0xffff)
				x->nuse++;
		}
	}
	if (!S->nacc && !S->nred && !S->nfold)
		return vv_no(S, "nothing to vectorize: no store and no sum");
	{
		bool st = S->nred != 0, fold = S->nfold != 0;

		for (uint32_t i = 0; i < S->nacc; i++)
			st |= S->acc[i].store;
		if (!st && !fold)
			return vv_no(S, "nothing to vectorize: no store and no sum");
		/* An ordered reduction takes its lanes out one by one, in order: by itself it is the
		 * scalar loop with a vector load in front.  Measured on arm64 (TSVC s314, s316: a
		 * maximum by compare and select, nine times slower than the scalar loop); x86-64 is
		 * left as its lowering was written. */
		if (!st && !X86(S->e->c))
			return vv_no(S, "only an ordered reduction: nothing to pack");
	}
	for (uint32_t i = 0; i < S->nred; i++)
		if (vv_get(S, S->red[i].p)->nuse != 1 || vv_get(S, S->red[i].upd)->nuse != 0)
			return vv_no(S, "a sum used inside the loop");
	if (!vv_depend(S))
		return false;
	/* the exit test: continue while u cc n */
	if (cond >= S->n0 || V->p[cond].in.op != VIR_OP_ICMP || V->p[cond].blk != H)
		return vv_no(S, "an exit test that is not a comparison in the header");
	u = vw_find(V, V->p[cond].in.a), n = vw_find(V, V->p[cond].in.b), cc = V->p[cond].in.flags & 15;
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
	if (ui->kind != VK_LIN || !ui->exact || ni->kind != VK_INV || (V->p[u].in.type != VIR_TYPE_I32 && V->p[u].in.type != VIR_TYPE_I64 && V->p[u].in.type != VIR_TYPE_PTR))
		return vv_no(S, "an exit test that is not linear against invariant");
	c = ui->c;
	if (c > 0 && (cc == 2 || cc == 3) && !ui->uns)
		incl = cc == 3;
	else if (c < 0 && (cc == 4 || cc == 5) && !ui->uns)
		incl = cc == 5;
	else if (c > 0 && cc == 6 && (ui->uns || V->p[u].in.type != VIR_TYPE_I32))
		incl = false;
	else if (c < 0 && cc == 8 && V->p[u].in.type != VIR_TYPE_I32)
		incl = false;
	else if (cc == 1 && !((c < 0 ? -c : c) & ((c < 0 ? -c : c) - 1)) && (c > 0 || !ui->uns))
		incl = false;	/* != : the span must also be a multiple of the step */
	else
		return vv_no(S, "an exit test that is not <, <=, >, >=, != in the direction of the step");
	if (V->p[u].in.type == VIR_TYPE_PTR && cc >= 2 && cc <= 5)
		return vv_no(S, "an exit test that is not <, <=, >, >=, != in the direction of the step");
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

		bool ptr = V->p[u].in.type == VIR_TYPE_PTR;

		if (V->p[u].in.type == VIR_TYPE_I32) {
			u0 = vv_ins(S, S->VPRE, VIR_OP_EXT, VIR_TYPE_I64, 2 | (ui->uns ? 0 : VIR_INSN_FLAG_MS), u0, 0, 0);
			n0 = vv_ins(S, S->VPRE, VIR_OP_EXT, VIR_TYPE_I64, 2 | (ui->uns ? 0 : VIR_INSN_FLAG_MS), n0, 0, 0);
		} else if (cc == 6 || cc == 8) {	/* (64 bits, unsigned: the span is the difference only when it is not negative) */
			more = cc == 6 ? vv_ins(S, S->VPRE, VIR_OP_ICMP, VIR_TYPE_I32, 7, u0, n0, 0) : vv_ins(S, S->VPRE, VIR_OP_ICMP, VIR_TYPE_I32, 7, n0, u0, 0);
		}
		for (uint32_t i = 0; i < S->none; i++) {	/* a step that is a value: 1 */
			uint32_t o = S->one[i], ot = V->p[o].in.type;
			uint32_t m = vv_ins(S, S->VPRE, VIR_OP_ICMP, VIR_TYPE_I32, 0, o, vw_iconst(e, ot == VIR_TYPE_I32 ? VIR_TYPE_I32 : VIR_TYPE_I64, 1), 0);

			more = more ? vv_ins(S, S->VPRE, VIR_OP_AND, VIR_TYPE_I32, 0, more, m, 0) : m;
		}
		d = c > 0 ? vv_ins(S, S->VPRE, ptr ? VIR_OP_PSUB : VIR_OP_SUB, VIR_TYPE_I64, 0, n0, u0, 0) : vv_ins(S, S->VPRE, ptr ? VIR_OP_PSUB : VIR_OP_SUB, VIR_TYPE_I64, 0, u0, n0, 0);
		if (cc == 1 && ac > 1) {
			uint32_t m = vv_ins(S, S->VPRE, VIR_OP_ICMP, VIR_TYPE_I32, 0, vv_ins(S, S->VPRE, VIR_OP_AND, VIR_TYPE_I64, 0, d, vw_iconst(e, VIR_TYPE_I64, ac - 1), 0), vw_iconst(e, VIR_TYPE_I64, 0), 0);

			more = more ? vv_ins(S, S->VPRE, VIR_OP_AND, VIR_TYPE_I32, 0, more, m, 0) : m;
		}
		if (incl)
			d = vv_ins(S, S->VPRE, VIR_OP_ADD, VIR_TYPE_I64, 0, d, vw_iconst(e, VIR_TYPE_I64, 1), 0);
		guard = vv_ins(S, S->VPRE, VIR_OP_ICMP, VIR_TYPE_I32, 5, d, vw_iconst(e, VIR_TYPE_I64, (int64_t)S->vf * ac), 0);
		if (more)
			guard = vv_ins(S, S->VPRE, VIR_OP_AND, VIR_TYPE_I32, 0, guard, more, 0);
		vv_term(S, S->VPRE, VIR_OP_BRIF, guard, S->VPH, S->J);
	}
	/* VPH: the iterations done on lanes */
	nv = d;
	if (ac != 1) {
		nv = vv_ins(S, S->VPH, VIR_OP_ADD, VIR_TYPE_I64, 0, nv, vw_iconst(e, VIR_TYPE_I64, ac - 1), 0);
		if (!(ac & (ac - 1))) {
			uint32_t shift = 0;
			for (int64_t v = ac; v > 1; v >>= 1) shift++;
			nv = vv_ins(S, S->VPH, VIR_OP_LSHR, VIR_TYPE_I64, 0, nv, vw_iconst(e, VIR_TYPE_I64, shift), 0);
		} else
			nv = vv_ins(S, S->VPH, VIR_OP_UDIV, VIR_TYPE_I64, 0, nv, vw_iconst(e, VIR_TYPE_I64, ac), 0);
	}
	nv = vv_ins(S, S->VPH, VIR_OP_AND, VIR_TYPE_I64, 0, nv, vw_iconst(e, VIR_TYPE_I64, -(int64_t)S->vf), 0);
	/* VB's parameters: operand 0 from VPH, 1 from VB (filled at the end) */
	cnt = vv_param(S, S->VB, VIR_TYPE_I64, nv, 0, 0);
	for (uint32_t i = 0; i < S->nind; i++) {
		uint32_t p = S->ind[i];

		S->indb[i] = vv_param(S, S->VB, V->p[p].in.type, vw_find(V, V->x[V->p[p].in.b + S->jP]), 0, 0);
	}
	for (uint32_t i = 0; i < S->nred; i++) {
		if (V->p[S->red[i].p].in.type == S->fty) {
			uint32_t p = S->red[i].p;
			S->red[i].acc = vv_param(S, S->VB, S->fty, vw_find(V, V->x[V->p[p].in.b + S->jP]), 0, 0);
			continue;
		}
		if (!z)
			z = vv_ins(S, S->VPH, VIR_OP_VSPLAT, vv_vty(S, S->ity), 0, vw_iconst(e, S->ity, 0), 0, 0);
		S->red[i].acc = vv_param(S, S->VB, vv_vty(S, S->ity), z, 0, 0);
	}
	for (uint32_t i = 0; i < S->nfold; i++) {
		uint32_t p = S->fold[i].p;
		S->fold[i].acc = vv_param(S, S->VB, V->p[p].in.type, vw_find(V, V->x[V->p[p].in.b + S->jP]), 0, 0);
	}
	for (uint32_t i = 0; i < S->ncarry; i++) {
		uint32_t p = S->carry[i].p;
		S->carry[i].par = vv_param(S, S->VB, V->p[p].in.type, vw_find(V, V->x[V->p[p].in.b + S->jP]), 0, 0);
	}
	/* the body */
	for (uint32_t i = 0; i < S->nlb && !S->why; i++)
		for (uint32_t q = V->b[S->lb[i]].head; q && !S->why; q = V->p[q].next) {
			vir_insn in;
			bool rev;
			uint32_t a, x;

			if (q >= S->n0 || vv_skip(V, q))
				continue;
			in = V->p[q].in;
			if (in.op == VIR_OP_LOAD && S->I[q].vec) continue;
			if (in.op == VIR_OP_LOAD && S->I[q].kind == VK_W32) {
				if (!(a = vv_addr(S, q, &rev))) break;
				x = vv_ins(S, S->VB, VIR_OP_LOAD, VIR_TYPE_I32X4, in.flags & ~(uint32_t)VIR_INSN_FLAG_MS, a, in.b, 0);
				if (rev) x = vv_ins(S, S->VB, VIR_OP_VREV, VIR_TYPE_I32X4, 0, x, 0, 0);
				S->I[q].vec = x;
			} else if (in.op == VIR_OP_LOAD && S->I[q].kind == VK_VEC) {
				if (vv_get(S, vw_find(V, in.a))->kind == VK_SCALAR) {
					x = 0;
					for (uint32_t lane = 0; lane < S->vf; lane++) {
						uint32_t budget = 256;
						if (!(a = vv_at(S, in.a, lane, &budget))) break;
						uint32_t val = vv_ins(S, S->VB, VIR_OP_LOAD, in.type, in.flags, a, in.b, 0);
						x = lane ? vv_ins(S, S->VB, VIR_OP_VINSERT, vv_vty(S, in.type), 0, x, val, lane) : vv_ins(S, S->VB, VIR_OP_VSPLAT, vv_vty(S, in.type), 0, val, 0, 0);
					}
					S->I[q].vec = x;
					continue;
				}
				if (!(a = vv_addr(S, q, &rev)))
					break;
				int64_t stride = vv_get(S, vw_find(V, in.a))->c;
				if (stride != (int64_t)S->esz && stride != -(int64_t)S->esz) {
					x = 0;
					for (uint32_t lane = 0; lane < S->vf; lane++) {
						uint32_t addr = lane ? vv_ins(S, S->VB, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, a, vw_iconst(e, VIR_TYPE_I64, lane * stride), 0) : a;
						uint32_t val = vv_ins(S, S->VB, VIR_OP_LOAD, in.type, in.flags, addr, in.b, 0);
						x = lane ? vv_ins(S, S->VB, VIR_OP_VINSERT, vv_vty(S, in.type), 0, x, val, lane) : vv_ins(S, S->VB, VIR_OP_VSPLAT, vv_vty(S, in.type), 0, val, 0, 0);
					}
				} else
					x = vv_ins(S, S->VB, VIR_OP_LOAD, vv_vty(S, in.type), in.flags & ~(uint32_t)VIR_INSN_FLAG_MS, a, in.b, 0);
				if (rev)
					x = vv_ins(S, S->VB, VIR_OP_VREV, vv_vty(S, in.type), 0, x, 0, 0);
				S->I[q].vec = x;
			} else if (in.op == VIR_OP_STORE) {
				if (!(x = vv_vec(S, in.b)) || !(a = vv_addr(S, q, &rev)))
					break;
				if (rev)
					x = vv_ins(S, S->VB, VIR_OP_VREV, V->p[x].in.type, 0, x, 0, 0);
				int64_t stride = vv_get(S, vw_find(V, in.a))->c;
				if (stride != (int64_t)S->esz && stride != -(int64_t)S->esz) {
					for (uint32_t lane = 0; lane < S->vf; lane++) {
						uint32_t addr = lane ? vv_ins(S, S->VB, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, a, vw_iconst(e, VIR_TYPE_I64, lane * stride), 0) : a;
						uint32_t val = vv_ins(S, S->VB, VIR_OP_VEXTRACT, V->p[in.b].in.type, 0, x, lane, 0);
						vv_ins(S, S->VB, VIR_OP_STORE, VIR_TYPE_VOID, in.flags, addr, val, 0);
					}
				} else
					vv_ins(S, S->VB, VIR_OP_STORE, VIR_TYPE_VOID, in.flags, a, x, 0);
			} else if (S->I[q].kind == VK_REDUPD) {
				for (uint32_t k = 0; k < S->nred; k++)
					if (S->red[k].upd == q) {
						if (!(x = vv_vec(S, S->red[k].x)))
							break;
						if (V->p[S->red[k].p].in.type == S->fty) {
							/* Preserve the scalar recurrence, including rounding at every step. */
							uint32_t acc = S->red[k].acc;
							bool left = vw_find(V, in.a) == S->red[k].p;
							for (uint32_t lane = 0; lane < S->vf; lane++) {
								uint32_t val = vv_ins(S, S->VB, VIR_OP_VEXTRACT, S->fty, 0, x, lane, 0);
								acc = vv_ins(S, S->VB, in.op, S->fty, in.flags, left ? acc : val, left ? val : acc, 0);
							}
							S->red[k].accnext = acc;
						} else
							S->red[k].accnext = vv_ins(S, S->VB, in.op, vv_vty(S, S->ity), 0, S->red[k].acc, x, 0);
					}
			}
		}
	if (S->why)
		return false;
	for (uint32_t i = 0; i < S->ncarry; i++) {
		if (!S->carry[i].last && !vv_vec(S, S->carry[i].p)) return false;
		V->x[V->p[S->carry[i].par].in.b + 1] = S->carry[i].last;
	}
	for(uint32_t i=0;i<S->nfold;i++) S->fold[i].cur=S->fold[i].acc;
	for(uint32_t lane=0;lane<S->vf;lane++) {
		uint32_t nx[8];
		for(uint32_t i=0;i<S->nfold;i++) {
			uint32_t budget=256;
			nx[i]=vv_foldat(S,S->fold[i].next,lane,&budget);
			if(!nx[i])return false;
		}
		for(uint32_t i=0;i<S->nfold;i++)S->fold[i].cur=nx[i];
	}
	for(uint32_t i=0;i<S->nfold;i++) {
		S->fold[i].last=S->fold[i].cur;
		V->x[V->p[S->fold[i].acc].in.b+1]=S->fold[i].last;
	}
	/* the inductions step four iterations; the counter */
	for (uint32_t i = 0; i < S->nind; i++) {
		uint32_t p = S->ind[i], ty = V->p[p].in.type;

		S->indn[i] = vv_ins(S, S->VB, ty == VIR_TYPE_PTR ? VIR_OP_PTRADD : VIR_OP_ADD, ty, 0, S->indb[i],
				    vw_iconst(e, ty == VIR_TYPE_I32 ? VIR_TYPE_I32 : VIR_TYPE_I64, (int64_t)((uint64_t)S->indk[i] * S->vf)), 0);
		V->x[V->p[S->indb[i]].in.b + 1] = S->indn[i];
	}
	for (uint32_t i = 0; i < S->nred; i++)
		V->x[V->p[S->red[i].acc].in.b + 1] = S->red[i].accnext;
	for (uint32_t i = 0; i < S->npin; i++)
		V->x[V->p[S->pin[i].par].in.b + 1] = vv_ins(S, S->VB, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, S->pin[i].par, vw_iconst(e, VIR_TYPE_I64, vv_get(S, S->pin[i].a)->c * S->vf), 0);
	cntn = vv_ins(S, S->VB, VIR_OP_ADD, VIR_TYPE_I64, 0, cnt, vw_iconst(e, VIR_TYPE_I64, -(int64_t)S->vf), 0);
	V->x[V->p[cnt].in.b + 1] = cntn;
	guard = vv_ins(S, S->VB, VIR_OP_ICMP, VIR_TYPE_I32, 1, cntn, vw_iconst(e, VIR_TYPE_I64, 0), 0);
	vv_term(S, S->VB, VIR_OP_BRIF, guard, S->VB, S->VX);
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
				if (V->p[p].in.type == S->fty) {
					fin = S->red[i].accnext;
					break;
				}
				fin = vv_ins(S, S->VX, VIR_OP_VREDUCE, S->ity, 0, S->red[i].accnext, 0, 0);
				fin = vv_ins(S, S->VX, VIR_OP_ADD, S->ity, 0, init, fin, 0);
			}
		for (uint32_t i = 0; i < S->ncarry; i++)
			if (S->carry[i].p == p) fin = S->carry[i].last;
		for (uint32_t i = 0; i < S->nfold; i++)
			if (S->fold[i].p == p) fin = S->fold[i].last;
		if (!fin || njp == 40)
			return vv_no(S, "a parameter without a final value");
		jp[njp++] = vv_param(S, S->J, V->p[p].in.type, init, fin, init);
	}
	vv_term(S, S->VX, VIR_OP_BR, S->J, 0, 0);
	vv_term(S, S->J, VIR_OP_BR, H, 0, 0);
	if (S->nchk) {	/* the pairs that may be one memory: any two ranges that meet, and the loop stays scalar */
		uint32_t span = vv_ins(S, S->VPH, VIR_OP_SHL, VIR_TYPE_I64, 0, nv, vw_iconst(e, VIR_TYPE_I64, S->lg), 0), any = 0;

		for (uint32_t i = 0; i < S->nchk; i++) {
			uint32_t l0, h0, l1, h1, m;

			if (!vv_range(S, S->chk[i].a, span, &l0, &h0) || !vv_range(S, S->chk[i].b, span, &l1, &h1))
				return false;
			m = vv_ins(S, S->VPH, VIR_OP_AND, VIR_TYPE_I32, 0, vv_ins(S, S->VPH, VIR_OP_ICMP, VIR_TYPE_I32, 6, l0, h1, 0),
				   vv_ins(S, S->VPH, VIR_OP_ICMP, VIR_TYPE_I32, 6, l1, h0, 0), 0);
			any = any ? vv_ins(S, S->VPH, VIR_OP_OR, VIR_TYPE_I32, 0, any, m, 0) : m;
		}
		vv_term(S, S->VPH, VIR_OP_BRIF, any, S->J, S->VB);
	} else {
		vv_term(S, S->VPH, VIR_OP_BR, S->VB, 0, 0);
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
	static _Thread_local int m = -1;

	if (m < 0) {
		const char *s = getenv("VCC_VIR_VEC");	/* 0: off; r: report each loop to stderr; n: no constants hoisted */

		m = s && !strcmp(s, "0") ? 0 : 1 | (s && strchr(s, 'r') ? 2 : 0) | (s && strchr(s, 'n') ? 4 : 0);
	}
	return m;
}

VPRIV void vw_vectorize(ecx *e)
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
		if (V->b[V->order[i]].npred == 2 && V->b[V->order[i]].term && V->p[V->b[V->order[i]].term].in.op == VIR_OP_BRIF)
			hs[nh++] = V->order[i];
	for (uint32_t i = 0; i < nh; i++) {
		uint32_t H = hs[i], t = V->b[H].term;
		bool found = false, done;

		for (int arm = 0; arm < 2 && !found; arm++) {
			uint32_t cur = arm ? V->p[t].in.c : V->p[t].in.b, prev = H, n = 1;

			S.lb[0] = H;
			while (cur != H && n < 64 && V->b[cur].npred == 1 && V->pr[V->b[cur].pred] == prev && V->b[cur].term &&
			       V->p[V->b[cur].term].in.op == VIR_OP_BR) {
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
		if (!(vv_env() & 4)) {
			if (done)
				vv_pin(&S, S.J);
			else if (V->p[V->b[S.P].term].in.op == VIR_OP_BR && V->p[V->b[S.P].term].in.a == H)
				vv_pin(&S, S.P);
		}
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
