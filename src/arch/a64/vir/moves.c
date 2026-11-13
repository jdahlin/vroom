/* arm64 lowering of VIR: failing out, registers and constants, the allocator's moves, spills and reloads, labels and the edges of branches. */

#include "lowering-internal.h"

VPRIV __attribute__((noreturn, cold)) void vl_fail(vl *L, const char *why)
{
	L->e->vfail = why;	/* (in ecx: L is not volatile across the jump) */
	_longjmp(L->e->bail, 3);
}

VPRIV void *vl_grow(void *p, uint32_t *cap, uint32_t n, size_t sz)
{
	if (n <= *cap)
		return p;
	*cap = n + n / 2 + 16;
	p = realloc(p, (size_t)*cap * sz);
	if (!p)
		abort();
	return p;
}

/* constant v into general register rd */
static void vl_mat(vl *L, uint32_t v, int rd)
{
	ecx *e = L->e;
	const vir_ins *k = &L->f->ins[v];

	switch (k->op) {
	case VO_ICONST:
		movi(e, 1, rd, k->ty == VT_I32 ? (int64_t)(uint32_t)k->b : vl_k64(k));
		return;
	case VO_FCONST:
		movi(e, 1, rd, vl_k64(k));
		return;
	case VO_SYMADDR:
		addr_of_ref(e, rd, k->a, vl_k64(&(vir_ins){ .b = k->b, .c = k->c }));
		return;
	case VO_SLOTADDR:
		opi(e, I_ADDI, 1, rd, FRAME, L->soff[k->a] + vl_k64(&(vir_ins){ .b = k->b, .c = k->c }));
		return;
	default:
		vl_fail(L, VIRFAIL(constant_not_materializable));
	}
}

/* constant v into float register vd, through general register t */
static void vl_matf(vl *L, uint32_t v, int vd, int t)
{
	const vir_ins *k = &L->f->ins[v];
	int d = k->ty == VT_F64;

	if (k->op != VO_FCONST)
		vl_fail(L, VIRFAIL(float_constant_not_fconst));
	if (!k->b && !k->c) {
		word(L->e, f_in(d, vd, ZR));
		return;
	}
	vl_mat(L, v, t);
	word(L->e, f_in(d, vd, t));
}

/* operand k as a general register: its register, else the constant or its slot in t */
VPRIV int vl_ix(vl *L, uint32_t k, int t)
{
	uint8_t r = L->opr[k];
	uint32_t v = L->ov[k];

	if (r != VR_NONE)
		return vl_gx(r);
	if (vl_zero(L->f, v))
		return ZR;
	if (vl_isconst(L->f, v)) {
		vl_mat(L, v, t);
		return t;
	}
	if (L->o->slot[v] != VIR_NOBLK) {
		mem(L->e, I_LD, vl_ssz(L, L->o->slot[v]), 0, t, FRAME, L->spoff[L->o->slot[v]]);
		return t;
	}
	vl_fail(L, VIRFAIL(operand_without_location));
}

VPRIV int vl_fx(vl *L, uint32_t k)
{
	if (L->opr[k] == VR_NONE)
		vl_fail(L, VIRFAIL(float_operand_without_register));
	return vl_gv(L->opr[k]);
}

static void vl_rmov(vl *L, uint8_t dst, uint8_t src)
{
	if (dst == src)
		return;
	if (vl_fcls(dst) != vl_fcls(src))
		vl_fail(L, VIRFAIL(move_between_register_classes));
	if (vl_fcls(dst))
		word(L->e, 0x4ea01c00u | (unsigned)vl_gv(src) << 16 | (unsigned)vl_gv(src) << 5 | (unsigned)vl_gv(dst));	/* mov v.16b, v.16b (a vector's upper half too) */
	else
		mov(L->e, 1, vl_gx(dst), vl_gx(src));
}

/* a vector register and its 16-byte spill slot: the slot's address in t, then ldr/str q */
static void vl_qslot(vl *L, bool load, int v, uint32_t s, int t)
{
	opi(L->e, I_ADDI, 1, t, FRAME, L->spoff[s]);
	word(L->e, (load ? 0x3dc00000u : 0x3d800000u) | (unsigned)t << 5 | (unsigned)v);
}

/* register dst <- spill slot s, through general register t for a float */
static void vl_reload(vl *L, uint8_t dst, uint32_t s, int t)
{
	int sz = vl_ssz(L, s);

	if (L->o->ssz[s] == 16) {
		vl_qslot(L, true, vl_gv(dst), s, t);
	} else if (vl_fcls(dst)) {
		mem(L->e, I_LDF, sz, 0, vl_gv(dst), FRAME, L->spoff[s]);
	} else {
		mem(L->e, I_LD, sz, 0, vl_gx(dst), FRAME, L->spoff[s]);
	}
}

VPRIV void vl_spill(vl *L, uint8_t src, uint32_t s, int t)
{
	int sz = vl_ssz(L, s);

	if (L->o->ssz[s] == 16) {
		vl_qslot(L, false, vl_gv(src), s, t);
	} else if (vl_fcls(src)) {
		mem(L->e, I_STF, sz, 0, vl_gv(src), FRAME, L->spoff[s]);
	} else {
		mem(L->e, I_ST, sz, 0, vl_gx(src), FRAME, L->spoff[s]);
	}
}

static void vl_remat(vl *L, uint8_t dst, uint32_t v, int t)
{
	if (vl_fcls(dst))
		vl_matf(L, v, vl_gv(dst), t);
	else
		vl_mat(L, v, vl_gx(dst));
}

/* -fvir-probe=MASK (measuring): a nop after every allocator edit of the kinds in MASK, so
 * that instructions retired minus an unprobed run's is how often that kind executes.
 * Bits: 0 reload, 1 remat, 2 move, 3 store (edits before an instruction); 4 remat, 5 store,
 * 6 reload, 7 move (on an edge).  An upper bound: the nop also stops the record folds. */
#define PROBE(bit) do { if (L->e->E->o->vir_aid.probe >> (bit) & 1) word(L->e, 0xd503201f); } while (0)

/* one allocator edit, before the instruction it is positioned at */
VPRIV void vl_edit(vl *L, const vir_edit *x)
{
	const vir_alloc_out *o = L->o;

	L->ei = (uint32_t)(x - o->edit) + 1;	/* (the next edit: what the instruction's folds look past) */
	switch (x->kind) {
	case VE_RELOAD:
		vl_reload(L, x->dst, o->slot[x->v], R_X16); PROBE(0);
		break;
	case VE_REMAT:
		vl_remat(L, x->dst, x->v, R_X16); PROBE(1);
		break;
	case VE_MOVE:
		vl_rmov(L, x->dst, (uint8_t)x->src); PROBE(2);
		break;
	case VE_STORE:
		vl_spill(L, x->src, o->slot[x->v], R_X16); PROBE(3);
		break;
	default:
		vl_fail(L, VIRFAIL(swapped_operands));
	}
}

/* edge g's sequenced parallel move */
VPRIV void vl_emoves(vl *L, uint32_t g)
{
	const vir_alloc_out *o = L->o;
	const vir_aedge *ed = &o->edge[g];
	int t = R_X16;

	for (uint32_t m = 0; m < ed->n; m++) {
		const vir_emove *mv = &o->move[ed->first + m];

		if ((!(mv->dst & VL_SLOT) && mv->dst == VR_SCR0) || (mv->kind != VE_REMAT && !(mv->src & VL_SLOT) && mv->src == VR_SCR0))
			t = R_X17;
	}
	for (uint32_t m = 0; m < ed->n; m++) {
		const vir_emove *mv = &o->move[ed->first + m];
		bool ds = mv->dst & VL_SLOT, ss = mv->src & VL_SLOT;
		uint32_t dn = mv->dst & ~VL_SLOT, sn = mv->src & ~VL_SLOT;

		if (t == R_X17 && (ds || ss || mv->kind == VE_REMAT))
			L->risk = true;
		switch (mv->kind) {
		case VE_REMAT:
			if (ds) {
				vl_mat(L, mv->v, t);
				mem(L->e, I_ST, vl_ssz(L, dn), 0, t, FRAME, L->spoff[dn]);
			} else {
				vl_remat(L, (uint8_t)dn, mv->v, t);
			}
			PROBE(4);
			break;
		case VE_MOVE:
			if (ds && ss && L->o->ssz[sn] == 16) {	/* (through v31: no move of this edge may hold it) */
				for (uint32_t k = 0; k < ed->n; k++) {
					const vir_emove *o2 = &o->move[ed->first + k];

					if ((!(o2->dst & VL_SLOT) && o2->dst == VR_SCR1) || (o2->kind != VE_REMAT && !(o2->src & VL_SLOT) && o2->src == VR_SCR1))
						vl_fail(L, VIRFAIL(vector_spill_move_scratch_taken));
				}
				vl_qslot(L, true, 31, sn, t);
				vl_qslot(L, false, 31, dn, t);
			} else if (ds && ss) {
				mem(L->e, I_LD, vl_ssz(L, sn), 0, t, FRAME, L->spoff[sn]);
				mem(L->e, I_ST, vl_ssz(L, dn), 0, t, FRAME, L->spoff[dn]);
			} else if (ds) {
				vl_spill(L, (uint8_t)sn, dn, t); PROBE(5);
			} else if (ss) {
				vl_reload(L, (uint8_t)dn, sn, t); PROBE(6);
			} else {
				vl_rmov(L, (uint8_t)dn, (uint8_t)sn); PROBE(7);
			}
			break;
		case VE_XCHG:
			if (ds || ss || vl_fcls((uint8_t)dn) != vl_fcls((uint8_t)sn))
				vl_fail(L, VIRFAIL(exchange_with_slot));
			if (vl_fcls((uint8_t)dn)) {
				unsigned a = (unsigned)vl_gv((uint8_t)dn), b = (unsigned)vl_gv((uint8_t)sn);

				word(L->e, 0x6e201c00u | b << 16 | a << 5 | a);	/* eor a.16b, a.16b, b.16b */
				word(L->e, 0x6e201c00u | a << 16 | b << 5 | b);
				word(L->e, 0x6e201c00u | b << 16 | a << 5 | a);
			} else {
				int a = vl_gx((uint8_t)dn), b = vl_gx((uint8_t)sn);

				op3(L->e, I_EOR, 1, a, a, b);
				op3(L->e, I_EOR, 1, b, b, a);
				op3(L->e, I_EOR, 1, a, a, b);
			}
			break;
		default:
			vl_fail(L, VIRFAIL(unknown_edge_move));
		}
	}
}

/* Block t's label for the code being emitted.  With a save point (shrink-wrapping), code
 * behind the prologue that goes to a block in front of it (a return: vl_order saw to that)
 * gets a second copy of the block, with the epilogue (VLT.lbf, emitted last by vl_body). */
VPRIV uint32_t vl_lbl(vl *L, uint32_t t)
{
	if (L->sp != VIR_NOBLK && !L->bare && !vir_dominates(L->f, L->sp, t)) {
		if (!VLT.lbf[t])
			VLT.lbf[t] = newlabel(L->e);
		return VLT.lbf[t];
	}
	return L->lbl[t];
}

/* a block every branch goes past (vl_chase): not emitted */
VPRIV bool vl_skip(vl *L, uint32_t b) { return b && vl_chase(L, b) != b; }

static uint32_t vl_next(vl *L, uint32_t b)	/* the block emitted after b */
{
	uint32_t p = L->opos[b], nb = L->f->nblk;

	while (++p < nb && vl_skip(L, L->ord[p]))
		;
	return p < nb ? L->ord[p] : nb;
}

/* the block the code being emitted falls through to (none: UINT32_MAX) */
VPRIV uint32_t vl_fnext(vl *L) { return L->fallok ? vl_next(L, L->at) : UINT32_MAX; }

/* the label a branch from block b to its successor k (block to) takes: the block's, or a
 * new one whose moves follow the block */
VPRIV uint32_t vl_succ(vl *L, uint32_t b, uint32_t k, uint32_t to)
{
	const vir_alloc_out *o = L->o;
	uint32_t g = o->eidx[o->soff[b] + k];

	if (g == VIR_NOBLK || !o->edge[g].n)
		return vl_lbl(L, vl_chase(L, to));
	if (L->ntr == sizeof L->tr / sizeof L->tr[0])
		vl_fail(L, VIRFAIL(too_many_edge_moves));
	L->tr[L->ntr].l = newlabel(L->e);
	L->tr[L->ntr].e = g;
	L->tr[L->ntr].to = to;
	return L->tr[L->ntr++].l;
}

VPRIV bool vl_plain(vl *L, uint32_t b, uint32_t k)	/* the edge has no moves */
{
	uint32_t g = L->o->eidx[L->o->soff[b] + k];

	return g == VIR_NOBLK || !L->o->edge[g].n;
}
