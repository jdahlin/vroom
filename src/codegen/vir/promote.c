/* Frame slots to variables: a slot whose address does not escape becomes SSA values. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/* ---------------- slots to variables ---------------- */

#define VW_NOCONV UINT32_MAX

typedef struct vwfld { uint32_t slot; int64_t rel; uint32_t size, ty; } vwfld;

/* the conversion a load of type ty (flags fl) applies to the stored value of type st: VW_NOCONV
 * none, VO_EXT/P2I/BITCAST one instruction; false if it takes more */
static bool vw_ldconv(uint32_t st, uint32_t ty, uint32_t fl, uint32_t *op)
{
	uint32_t w = VIF_W(fl);

	/* A narrow load, or an i32 field read into i64, extends its stored bits. */
	if (w < 2 || (fl & VIF_MS) || (w == 2 && st == VT_I32 && ty == VT_I64)) {
		*op = VO_EXT;
		return st == VT_I32 && (ty == VT_I32 || ty == VT_I64);
	}
	if (st == ty)
		*op = VW_NOCONV;
	else if (st == VT_PTR && ty == VT_I64)
		*op = VO_P2I;
	else if (st == VT_I64 && ty == VT_PTR)
		*op = VO_I2P;
	else if ((st == VT_I32 && ty == VT_F32) || (st == VT_F32 && ty == VT_I32) ||
		 (st == VT_I64 && ty == VT_F64) || (st == VT_F64 && ty == VT_I64))
		*op = VO_BITCAST;
	else
		return false;
	return true;
}

static int64_t vw_rel(const vwp *a) { return (int64_t)((uint64_t)a->in.b | (uint64_t)a->in.c << 32); }

static void vw_escape(vwst *V, uint8_t *bad, uint32_t v)
{
	const vwp *q = &V->p[vw_find(V, v)];

	if (q->in.op == VO_SLOTADDR && q->blk == VW_NOBLK)
		bad[q->in.a] = 1;
}

/* var's value on entry to b (b's own definitions come later in it) */
static uint32_t vw_entry(ecx *e, uint32_t var, uint32_t b, uint32_t ty, bool defs)
{
	vwst *V = e->vw;
	uint32_t *d, p;

	if (!defs)
		return vw_readv(e, var, b, ty);
	if (!V->b[b].npred)
		return vw_undef(e, ty);
	if (V->b[b].npred == 1)
		return vw_readv(e, var, V->pr[V->b[b].pred], ty);
	d = vw_hslot(e, b, var | 0x80000000u, false);
	if (d)
		return vw_find(V, *d);
	p = vw_newphi(e, b, var, ty);
	*vw_hslot(e, b, var | 0x80000000u, true) = p;
	vw_phiops(e, p, b);
	return vw_find(V, p);
}

static uint32_t vw_zerok(ecx *e, uint32_t ty)
{
	return ty == VT_F32 || ty == VT_F64 ? vw_k(e, VO_FCONST, ty, 0, 0, 0) : vw_iconst(e, ty, 0);
}

/* memset q, of a constant length with 0: the bytes (len) it zeroes */
static bool vw_zeroset(vwst *V, uint32_t q, int64_t *len)
{
	int64_t z;

	return V->p[q].in.op == VO_MEMSET && vw_kint(V, vw_find(V, V->p[q].in.b), &z) && !z &&
	       vw_kint(V, vw_find(V, V->p[q].in.c), len) && *len > 0;
}

struct vwms { uint32_t q, slot; int64_t rel, len; };	/* a memset of a slot with 0 */

static bool vw_mscov(const struct vwms *m, const vwfld *f)
{
	return f->slot == m->slot && m->rel <= f->rel && f->rel + (int64_t)f->size <= m->rel + m->len;
}

/* a store the promotion made dead (a memset of a promoted slot too) */
VPRIV bool vw_promdead(const vwst *V, uint32_t q)
{
	const vwp *x = &V->p[q];

	return (x->in.op == VO_STORE || x->in.op == VO_MEMSET) && x->slot && V->sl[x->slot - 1].prom;
}

/* a slot whose address does not escape, read and written only as fields (offset, size) that
 * do not overlap, becomes one variable per field: its loads read the value the field holds
 * (Braun, as the walker's locals), its stores go */
VPRIV bool vw_promote(ecx *e)
{
	vwst *V = e->vw;
	vwfld *fd = NULL;
	uint32_t nf = 0, cf = 0, *fof, *cur, *cst, np0 = V->np, var0;
	uint32_t *sf, *fnx = NULL, cfnx = 0;	/* a slot's fields: first + 1 per slot, next + 1 per field */
	uint8_t *bad;
	bool done = false;
	int64_t rel;
	struct vwms *ms = NULL;	/* the memsets of slots with 0 */
	uint32_t nms = 0, cms = 0;

	if (!V->nsl)
		return false;
	bad = calloc(V->nsl, 1);
	sf = calloc(V->nsl, sizeof *sf);
	fof = malloc((size_t)V->np * sizeof *fof);	/* access -> field + 1 */
	if (!bad || !fof || !sf)
		vw_fail(e, VIRFAIL(out_of_memory));
	for (uint32_t s = 0; s < V->nsl; s++)
		bad[s] = V->sl[s].prom;	/* (an earlier run's: its stores are dead, its loads gone) */
	/* a slot escapes when its address is anything but the address of a load or store (the
	 * walker's exposed flag is set by any address taken, *(T *)&x too) */
	for (uint32_t i = 0; i < np0; i++) {
		const vwp *q = &V->p[i];
		const vir_opinfo *oi = &vir_optab[q->in.op];

		if (q->blk == VW_NOBLK || vw_find(V, i) != i || vw_promdead(V, i))
			continue;	/* (a store to a slot promoted before is dead) */
		if (q->in.op == VO_BPARAM) {
			if (q->in.b != VW_NOBLK)
				for (uint32_t j = 0; j < V->b[q->blk].npred; j++)
					vw_escape(V, bad, V->x[q->in.b + j]);
			continue;
		}
		for (int k = 0; k < 3; k++)
			if (vw_isval(oi->k[k]) && !(q->in.op == VO_BRIF && k > 0) &&
			    !(k == 0 && (q->in.op == VO_LOAD || q->in.op == VO_STORE || vw_zeroset(V, i, &rel))))
				vw_escape(V, bad, (&q->in.a)[k]);
		if (q->in.op == VO_CALL || q->in.op == VO_RET)
			for (uint32_t k = 0; k < V->x[q->in.c]; k++)
				vw_escape(V, bad, V->x[q->in.c + 1 + k]);
	}
	/* the fields, and what rules a slot out */
	for (uint32_t i = 0; i < np0; i++) {
		vwp *q = &V->p[i];
		uint32_t s, size, ty, k, a;
		int64_t len;

		fof[i] = 0;
		if ((q->in.op != VO_LOAD && q->in.op != VO_STORE && q->in.op != VO_MEMSET) ||
		    q->blk == VW_NOBLK || vw_find(V, i) != i)
			continue;
		a = vw_find(V, q->in.a);
		if (V->p[a].in.op != VO_SLOTADDR)
			continue;
		s = V->p[a].in.a;
		q->in.a = a;
		q->slot = s + 1;	/* (also an access through the address taken) */
		if (bad[s])
			continue;
		if (q->in.op == VO_MEMSET) {	/* zeroes the fields it covers: checked once all are known */
			if (!vw_zeroset(V, i, &len)) {
				bad[s] = 1;
				continue;
			}
			GROW(ms, nms, cms, 1);
			ms[nms] = (struct vwms){ i, s, vw_rel(&V->p[a]), len };
			fof[i] = UINT32_MAX - nms++;	/* (a memset: its index in ms) */
			continue;
		}
		if (q->in.fl & VIF_VOL) {
			bad[s] = 1;
			continue;
		}
		rel = vw_rel(&V->p[a]);
		size = 1u << VIF_W(q->in.fl);
		ty = q->in.op == VO_STORE ? V->p[q->in.b].in.ty : VT_VOID;
		/* Simplification can remove a truncation before a narrow store.
		 * Its field still holds only the stored low bits, even when another
		 * store uses an i32 carrier. Normalize before comparing field types. */
		if (size <= 4 && ty == VT_I64)
			ty = VT_I32;
		for (k = sf[s] - 1; k != UINT32_MAX; k = fnx[k] - 1) {	/* (the slot's own fields only) */
			vwfld *f = &fd[k];

			if (f->rel == rel && f->size == size)
				break;
			if (f->rel < rel + (int64_t)size && rel < f->rel + (int64_t)f->size)
				bad[s] = 1;	/* overlapping fields */
		}
		if (bad[s])
			continue;
		if (k == UINT32_MAX) {
			k = nf;
			GROW(fd, nf, cf, 1);
			GROW(fnx, nf, cfnx, 1);
			fnx[nf] = sf[s];
			sf[s] = nf + 1;
			fd[nf++] = (vwfld){ s, rel, size, VT_VOID };
		}
		if (ty != VT_VOID) {
			if (fd[k].ty != VT_VOID && fd[k].ty != ty) {
				if ((fd[k].ty == VT_PTR || fd[k].ty == VT_I64) && (ty == VT_PTR || ty == VT_I64))
					ty = VT_I64;	/* a pointer and its integer (p++ as p2i, add, i2p) */
				else
					bad[s] = 1;	/* stores of different types */
			}
			fd[k].ty = ty;
		}
		fof[i] = k + 1;
	}
	/* a memset covers each field of its slot whole or not at all */
	for (uint32_t m = 0; m < nms; m++)
		for (uint32_t k = sf[ms[m].slot] - 1; k != UINT32_MAX; k = fnx[k] - 1) {
			const vwfld *f = &fd[k];
			int64_t lo = ms[m].rel, hi = ms[m].rel + ms[m].len;

			if (f->slot == ms[m].slot && f->rel < hi && lo < f->rel + (int64_t)f->size &&
			    !(lo <= f->rel && f->rel + (int64_t)f->size <= hi))
				bad[f->slot] = 1;
		}
	/* every load must take the field's stored type with one conversion at most */
	for (uint32_t i = 0; i < V->np; i++) {
		vwp *q = &V->p[i];
		uint32_t op;

		if (!fof[i] || q->in.op != VO_LOAD || bad[q->slot - 1])
			continue;
		vwfld *f = &fd[fof[i] - 1];

		if (f->ty == VT_VOID)	/* never stored: as the first load reads it */
			f->ty = VIF_W(q->in.fl) < 2 || (q->in.fl & VIF_MS) ? VT_I32 : q->in.ty;
		if (!vw_ldconv(f->ty, q->in.ty, q->in.fl, &op))
			bad[q->slot - 1] = 1;
	}
	{
		uint32_t any = 0;

		for (uint32_t k = 0; k < nf; k++)
			any += !bad[fd[k].slot];
		if (!any)
			goto out;
	}
	/* Normalize each promoted store to its field's carrier: pointer/integer
	 * fields use p2i, narrow integer fields truncate an i64 source. Place
	 * the conversion after the store, which promotion makes dead. */
	for (uint32_t i = 0; i < np0; i++) {
		uint32_t p, b;

		if (V->p[i].in.op != VO_STORE || !fof[i] || bad[V->p[i].slot - 1] || vw_find(V, i) != i)
			continue;
		uint32_t to = fd[fof[i] - 1].ty, from = V->p[V->p[i].in.b].in.ty, op;
		if (to == VT_I64 && from == VT_PTR)
			op = VO_P2I;
		else if (to == VT_I32 && from == VT_I64)
			op = VO_TRUNC;
		else
			continue;
		b = V->p[i].blk;
		p = vw_put(e, op, to, 0, V->p[i].in.b, 0, 0, b);
		V->p[p].next = V->p[i].next;
		V->p[i].next = p;
		if (V->b[b].tail == i)
			V->b[b].tail = p;
		V->p[i].in.b = p;
	}
	/* the variables: VW_VAR0 + nloc + earlier runs' + field */
	var0 = V->nloc + V->nprom;
	GROW(V->vty, var0, V->cvty, nf);
	for (uint32_t k = 0; k < nf; k++)
		V->vty[var0 + k] = (uint8_t)fd[k].ty;
	V->nprom += nf;
	cur = malloc((size_t)nf * 2 * sizeof *cur);
	if (!cur)
		vw_fail(e, VIRFAIL(out_of_memory));
	cst = cur + nf;
	/* each block's last store to a field is the field's value at its end */
	for (uint32_t oi = 0; oi < V->norder; oi++) {
		uint32_t b = V->order[oi];

		for (uint32_t q = V->b[b].head; q; q = V->p[q].next) {
			if (q >= np0 || !fof[q] || bad[V->p[q].slot - 1] || V->p[q].in.op == VO_LOAD || vw_find(V, q) != q)
				continue;
			if (V->p[q].in.op == VO_STORE) {
				uint32_t var = VW_VAR0 + var0 + fof[q] - 1;

				vw_write(e, var, b, V->p[q].in.b);
				*vw_hslot(e, b, var | 0x40000000u, true) = 1;	/* b defines var */
				continue;
			}
			for (uint32_t k = sf[V->p[q].slot - 1] - 1; k != UINT32_MAX; k = fnx[k] - 1) {
				uint32_t var = VW_VAR0 + var0 + k;

				if (!vw_mscov(&ms[UINT32_MAX - fof[q]], &fd[k]))
					continue;
				vw_write(e, var, b, vw_zerok(e, fd[k].ty));
				*vw_hslot(e, b, var | 0x40000000u, true) = 1;
			}
		}
	}
	for (uint32_t k = 0; k < nf; k++)
		cst[k] = UINT32_MAX;
	for (uint32_t oi = 0; oi < V->norder; oi++) {
		uint32_t b = V->order[oi];

		for (uint32_t q = V->b[b].head; q; q = V->p[q].next) {
			vwp *x = &V->p[q];
			uint32_t k, var, v, op;

			if (q >= np0 || !fof[q] || bad[x->slot - 1] || vw_find(V, q) != q)
				continue;
			if (x->in.op == VO_MEMSET) {
				const struct vwms *m = &ms[UINT32_MAX - fof[q]];

				for (k = sf[m->slot] - 1; k != UINT32_MAX; k = fnx[k] - 1)
					if (vw_mscov(m, &fd[k])) {
						cur[k] = vw_zerok(e, fd[k].ty);
						cst[k] = oi;
					}
				continue;
			}
			k = fof[q] - 1;
			var = VW_VAR0 + var0 + k;
			if (x->in.op == VO_STORE) {
				cur[k] = x->in.b;
				cst[k] = oi;
				continue;
			}
			if (cst[k] == oi) {
				v = cur[k];
			} else {
				v = vw_entry(e, var, b, fd[k].ty, vw_hslot(e, b, var | 0x40000000u, false) != NULL);
				x = &V->p[q];	/* (vw_entry can grow V->p) */
			}
			vw_ldconv(fd[k].ty, x->in.ty, x->in.fl, &op);
			if (op == VW_NOCONV) {
				V->fwd[q] = v;
			} else {
				x->in.op = op;
				x->in.fl = op == VO_EXT ? (x->in.fl & (3 | VIF_MS)) : 0;
				x->in.a = v;
				x->in.b = x->in.c = 0;
				x->slot = 0;
			}
		}
	}
	free(cur);
	for (uint32_t k = 0; k < nf; k++)
		if (!bad[fd[k].slot])
			V->sl[fd[k].slot].prom = 1, done = true;
out:
	free(ms);
	free(fd);
	free(fnx);
	free(sf);
	free(fof);
	free(bad);
	return done;
}
