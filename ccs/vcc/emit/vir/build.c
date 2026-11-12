/*
 * The VIR builder: the walker's lowering interface (emit/walk/proto.h) as a
 * virtual target, vw_ (SPEC-vir.md section 19, PLAN-vir.md V2).  The walker
 * is compiled a second time against it (emit/vir/walk.c); what it builds is
 * a function in VIR, handed to vir_canon and from there to the allocator and
 * a real target's lowering (emit/vir/a64.c).
 *
 * The walker's registers are names (X9): 0-191 integer, 192-254 float, 255
 * the result.  A name, and every local the walker gives us (LO(local), X3),
 * is a variable; reads and writes go through Braun et al.'s construction
 * ("Simple and Efficient Construction of SSA Form", CC 2013) on a pre-IR of
 * our own: every block is unsealed until the walk is done, a read with no
 * definition in its block makes a placeholder parameter, and at the end the
 * blocks are sealed in order and trivial parameters removed.  Then the
 * function is emitted into VIR in reverse postorder.
 *
 * A value in a name has the type of the operation that made it.  An i32
 * value stands for a register whose upper half is zero (the walker's x86
 * model: a 32-bit operation zero-extends), so widening one is a zero
 * extension; signed narrow loads give i64.  Pointers are ptr; arithmetic on
 * them is ptradd.
 *
 * Frame slots (X2): every hand-out of a frame offset makes a slot object
 * that owns the range until the next hand-out over it.  Per block a table
 * keeps the value last stored to each slot range (X4): a load from it,
 * including a call's argument words, gets that value.  Stores to a slot
 * whose address never escapes and that no load reads any more are dropped.
 *
 * What the builder does not handle makes the function fall back to the
 * direct emitter (X8): vw_fail, and efatal while e->vtry is set, longjmp to
 * the walker's bail; vw_try then reports false.
 */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "../../vir/vir.h"
#include "../../lib/vstack.h"
#include "vw.h"

/* ---------------- state ---------------- */

#define VW_NONAME 255
#define VW_FNAME0 192	/* float names 192..254 */
#define VW_RETV 255	/* the result variable */
#define VW_VAR0 256	/* locals: VW_VAR0 + k */
#define VW_NOBLK 0xffffffffu

static vwst *vw_st(ecx *e)
{
	if (!e->vw)
		e->vw = calloc(1, sizeof(vwst));
	return e->vw;
}

#define GROW(p, n, cap, k) do { \
	if ((uint64_t)(n) + (k) > (cap)) { \
		size_t nc_ = (cap) ? (size_t)(cap) * 2 : 256; \
		while (nc_ < (uint64_t)(n) + (k)) nc_ *= 2; \
		void *q_ = realloc((p), nc_ * sizeof *(p)); \
		if (!q_) vw_fail(e, "out of memory"); \
		(p) = q_; (cap) = (uint32_t)nc_; \
	} } while (0)

static __attribute__((noreturn, cold)) void vw_fail(ecx *e, const char *why)
{
	if (!e->vfail)
		e->vfail = why;
	_longjmp(e->bail, 1);
}

/* ---------------- pre-IR ---------------- */

static bool vw_isval(uint8_t k)
{
	return k >= OK_T && k <= OK_BITC ? true : k == OK_CONSTV;
}

static uint32_t vw_put(ecx *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c, uint32_t blk)
{
	vwst *V = e->vw;
	uint32_t i = V->np;

	GROW(V->p, V->np, V->cp, 1);
	V->np++;
	V->p[i] = (vwp){ { (uint8_t)op, (uint8_t)ty, (uint16_t)fl, a, b, c }, blk, 0, e->curloc, 0 };
	if (V->sealing) {
		GROW(V->fwd, i, V->cfwd, 1);
		V->fwd[i] = i;
	}
	return i;
}

/* an instruction at the end of block b's list */
static uint32_t vw_ins_in(ecx *e, uint32_t b, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t bb, uint32_t c)
{
	vwst *V = e->vw;
	uint32_t i = vw_put(e, op, ty, fl, a, bb, c, b);
	vwb *k = &V->b[b];

	if (k->tail)
		V->p[k->tail].next = i;
	else
		k->head = i;
	k->tail = i;
	return i;
}

static uint32_t vw_ins(ecx *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c)
{
	return vw_ins_in(e, e->vw->cur, op, ty, fl, a, b, c);
}

static uint32_t vw_k(ecx *e, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	return vw_put(e, op, ty, 0, a, b, c, VW_NOBLK);
}

static uint32_t vw_iconst(ecx *e, uint32_t ty, int64_t v)
{
	if (ty == VT_I32)
		return vw_k(e, VO_ICONST, VT_I32, 0, (uint32_t)v, 0);
	return vw_k(e, VO_ICONST, ty, 0, (uint32_t)v, (uint32_t)((uint64_t)v >> 32));
}

static uint32_t vw_ty(ecx *e, uint32_t v) { return e->vw->p[v].in.ty; }

static uint32_t vw_xput(ecx *e, const uint32_t *w, uint32_t n)
{
	vwst *V = e->vw;
	uint32_t o = V->nx;

	GROW(V->x, V->nx, V->cx, n ? n : 1);
	if (n)
		memcpy(&V->x[o], w, n * sizeof *w);
	V->nx += n;
	return o;
}

static uint32_t vw_newblk(ecx *e)
{
	vwst *V = e->vw;
	uint32_t b = V->nb;

	GROW(V->b, V->nb, V->cb, 1);
	V->nb++;
	V->b[b] = (vwb){ 0 };
	V->b[b].fin = VW_NOBLK;
	return b;
}

static uint32_t vw_lblk(ecx *e, uint32_t l)
{
	vwst *V = e->vw;

	if (l >= V->clab) {
		uint32_t nc = V->clab ? V->clab : 256;

		while (nc <= l)
			nc *= 2;
		V->lab = realloc(V->lab, nc * sizeof *V->lab);
		if (!V->lab)
			vw_fail(e, "out of memory");
		memset(V->lab + V->clab, 0xff, (nc - V->clab) * sizeof *V->lab);
		V->clab = nc;
	}
	if (V->lab[l] == VW_NOBLK)
		V->lab[l] = vw_newblk(e);
	return V->lab[l];
}

/* ends the current block with terminator t; code after it goes to a block nothing reaches */
static void vw_term(ecx *e, uint32_t op, uint32_t a, uint32_t b, uint32_t c, uint32_t fl)
{
	vwst *V = e->vw;

	V->b[V->cur].term = vw_put(e, op, VT_VOID, fl, a, b, c, V->cur);
	V->cur = vw_newblk(e);
	V->nst = 0;
}

static void vw_br(ecx *e, uint32_t blk) { vw_term(e, VO_BR, blk, 0, 0, 0); }

/* ---------------- variables (Braun) ---------------- */

static uint64_t vw_hkey(uint32_t b, uint32_t var) { return (uint64_t)b << 32 | var; }

static uint32_t *vw_hslot(ecx *e, uint32_t b, uint32_t var, bool add)
{
	vwst *V = e->vw;
	uint64_t key = vw_hkey(b, var) + 1;

	if (add && (V->hn + 1) * 2 > V->hcap) {
		uint32_t oc = V->hcap, nc = oc ? oc * 2 : 4096;
		vwh *old = V->h;

		V->h = calloc(nc, sizeof *V->h);
		if (!V->h)
			vw_fail(e, "out of memory");
		V->hcap = nc;
		for (uint32_t i = 0; i < oc; i++)
			if (old[i].key) {
				uint32_t j = (uint32_t)(old[i].key * 0x9e3779b97f4a7c15ull >> 40) & (nc - 1);

				while (V->h[j].key)
					j = (j + 1) & (nc - 1);
				V->h[j] = old[i];
			}
		free(old);
	}
	if (!V->hcap)
		return NULL;
	for (uint32_t j = (uint32_t)(key * 0x9e3779b97f4a7c15ull >> 40) & (V->hcap - 1);; j = (j + 1) & (V->hcap - 1)) {
		if (V->h[j].key == key)
			return &V->h[j].v;
		if (!V->h[j].key) {
			if (!add)
				return NULL;
			V->h[j].key = key;
			V->hn++;
			return &V->h[j].v;
		}
	}
}

static uint32_t vw_find(vwst *V, uint32_t v)
{
	uint32_t r = v;

	while (V->fwd[r] != r)
		r = V->fwd[r];
	while (V->fwd[v] != r) {
		uint32_t n = V->fwd[v];

		V->fwd[v] = r;
		v = n;
	}
	return r;
}

static void vw_write(ecx *e, uint32_t var, uint32_t b, uint32_t v)
{
	*vw_hslot(e, b, var, true) = v;
	e->vw->b[b].def = 1;
}

/* the type a parameter of var gets when its value has type ty */
static uint32_t vw_phity(ecx *e, uint32_t var, uint32_t ty)
{
	if (var >= VW_VAR0)
		return e->vw->vty[var - VW_VAR0];
	if (ty == VT_I32)
		return VT_I64;
	return ty;
}

static uint32_t vw_conv_in(ecx *e, uint32_t b, uint32_t v, uint32_t to);
VPRIV bool vw_emit(ecx *e);

static uint32_t vw_newphi(ecx *e, uint32_t b, uint32_t var, uint32_t ty)
{
	vwst *V = e->vw;
	uint32_t i = vw_put(e, VO_BPARAM, ty, 0, var, VW_NOBLK, 0, b);
	vwb *k = &V->b[b];

	if (k->ptail)
		V->p[k->ptail].next = i;
	else
		k->phead = i;
	k->ptail = i;
	return i;
}

static uint32_t vw_readv(ecx *e, uint32_t var, uint32_t b, uint32_t ty);

/* the operands of parameter p (block b) from b's predecessors, converted to its type */
static void vw_phiops(ecx *e, uint32_t p, uint32_t b)
{
	vwst *V = e->vw;
	uint32_t var = V->p[p].in.a, ty = V->p[p].in.ty, np = V->b[b].npred;
	uint32_t o = vw_xput(e, NULL, 0);

	GROW(V->x, V->nx, V->cx, np ? np : 1);
	V->nx += np;
	V->p[p].in.b = o;	/* (set first: a cycle back here reads p itself) */
	for (uint32_t k = 0; k < np; k++) {
		uint32_t q = V->pr[V->b[b].pred + k];
		uint32_t v = vw_readv(e, var, q, ty);

		V->x[o + k] = vw_conv_in(e, q, v, ty);
	}
}

static uint32_t vw_undef(ecx *e, uint32_t ty)
{
	if (ty == VT_F32 || ty == VT_F64)
		return vw_k(e, VO_FCONST, ty, 0, 0, 0);
	return vw_iconst(e, ty, 0);
}

/* var's value at the end of block b (during the walk: at the current point of the current block) */
static uint32_t vw_readv(ecx *e, uint32_t var, uint32_t b, uint32_t ty)
{
	vwst *V = e->vw;
	uint32_t *d = vw_hslot(e, b, var, false), v;

	if (d)
		return V->sealing ? vw_find(V, *d) : *d;
	if (!V->sealing) {	/* during the walk every block is open: a placeholder */
		v = vw_newphi(e, b, var, vw_phity(e, var, ty));
		vw_write(e, var, b, v);
		return v;
	}
	/* one level per block between a read and the definition it reaches (through vw_phiops
	 * at a join): a function of tens of thousands of blocks in a row goes the ordinary way */
	if (vstack_low())
		vw_fail(e, "VIR: a variable read too many blocks from its definition");
	if (!V->b[b].npred) {
		v = vw_undef(e, vw_phity(e, var, ty));
	} else if (V->b[b].npred == 1) {
		v = vw_readv(e, var, V->pr[V->b[b].pred], ty);
	} else {
		v = vw_newphi(e, b, var, vw_phity(e, var, ty));
		vw_write(e, var, b, v);
		vw_phiops(e, v, b);
		return vw_find(V, v);
	}
	vw_write(e, var, b, v);
	return v;
}

/* ---------------- conversions ---------------- */

static uint32_t vw_conv_in(ecx *e, uint32_t b, uint32_t v, uint32_t to)
{
	vwst *V = e->vw;
	uint32_t from = V->p[v].in.ty;

	if (from == to)
		return v;
	if (V->p[v].in.op == VO_ICONST && to != VT_F32 && to != VT_F64) {
		uint64_t x = V->p[v].in.b | (uint64_t)V->p[v].in.c << 32;

		return vw_iconst(e, to, to == VT_I32 ? (int64_t)(uint32_t)x : (int64_t)x);
	}
#define I(op, t, fl, a) vw_ins_in(e, b, op, t, fl, a, 0, 0)
	switch (from * 8 + to) {
	case VT_I32 * 8 + VT_I64: return I(VO_EXT, VT_I64, 2, v);
	case VT_I64 * 8 + VT_I32: return I(VO_TRUNC, VT_I32, 0, v);
	case VT_PTR * 8 + VT_I64: return I(VO_P2I, VT_I64, 0, v);
	case VT_I64 * 8 + VT_PTR: return I(VO_I2P, VT_PTR, 0, v);
	case VT_I32 * 8 + VT_PTR: return I(VO_I2P, VT_PTR, 0, I(VO_EXT, VT_I64, 2, v));
	case VT_PTR * 8 + VT_I32: return I(VO_TRUNC, VT_I32, 0, I(VO_P2I, VT_I64, 0, v));
	case VT_I32 * 8 + VT_F32: case VT_I64 * 8 + VT_F64:
	case VT_F32 * 8 + VT_I32: case VT_F64 * 8 + VT_I64: return I(VO_BITCAST, to, 0, v);
	case VT_I64 * 8 + VT_F32: return I(VO_BITCAST, VT_F32, 0, I(VO_TRUNC, VT_I32, 0, v));
	case VT_F32 * 8 + VT_I64: return I(VO_EXT, VT_I64, 2, I(VO_BITCAST, VT_I32, 0, v));
	default: break;
	}
#undef I
	vw_fail(e, "VIR: a value of one type read as another");
}

static uint32_t vw_conv(ecx *e, uint32_t v, uint32_t to) { return vw_conv_in(e, e->vw->cur, v, to); }

/* name r's value as type ty */
static uint32_t vw_get(ecx *e, int r, uint32_t ty)
{
	if (r < 0 || r >= VW_NONAME)
		vw_fail(e, "VIR: no such register");
	return vw_conv(e, vw_readv(e, (uint32_t)r, e->vw->cur, ty), ty);
}

static uint32_t vw_getraw(ecx *e, int r)	/* name r's value as it is */
{
	vwst *V = e->vw;
	uint32_t *d = vw_hslot(e, V->cur, (uint32_t)r, false);

	if (d)
		return *d;
	return vw_readv(e, (uint32_t)r, V->cur, r >= VW_FNAME0 ? VT_F64 : VT_I64);
}

static void vw_set(ecx *e, int r, uint32_t v)
{
	if (r < 0 || r >= VW_NONAME)
		vw_fail(e, "VIR: no such register");
	if (r >= VW_FNAME0)
		e->vw->fprod[r - VW_FNAME0] = 0;
	vw_write(e, (uint32_t)r, e->vw->cur, v);
}

static uint32_t vw_ity(int size) { return size == 8 ? VT_I64 : VT_I32; }

/* a walker value as a VIR value of type ty */
static uint32_t vw_val(ecx *e, const val *v, uint32_t ty)
{
	switch (v->k) {
	case V_CONST:
		if (ty == VT_F32 || ty == VT_F64)
			vw_fail(e, "VIR: an integer constant as a float");
		return vw_iconst(e, ty, (int64_t)v->i);
	case V_ADDR: {
		uint32_t a = vw_k(e, VO_SYMADDR, VT_PTR, v->ref, (uint32_t)(uint64_t)v->i, (uint32_t)((uint64_t)v->i >> 32));

		note_ref(e, v->ref);
		return vw_conv(e, a, ty);
	}
	case V_REG:
		return vw_get(e, v->r, ty);
	default:
		vw_fail(e, "VIR: this value kind");
	}
}

/* the type a value of the walker's has (V_REG: its name's current value's) */
static uint32_t vw_vty(ecx *e, const val *v)
{
	if (v->k == V_ADDR)
		return VT_PTR;
	if (v->k == V_REG)
		return vw_ty(e, vw_getraw(e, v->r));
	return VT_I64;
}

/* ---------------- registers ---------------- */

VPRIV int vw_treg(ecx *e)
{
	vwst *V = e->vw;

	for (int w = 0; w < 3; w++)
		if (~V->live[w]) {
			int k = __builtin_ctzll(~V->live[w]);

			V->live[w] |= 1ull << k;
			return w * 64 + k;
		}
	vw_fail(e, "VIR: out of names");
}

VPRIV int vw_ftreg(ecx *e)
{
	vwst *V = e->vw;

	for (int r = VW_FNAME0; r < VW_NONAME; r++)
		if (!(V->flive >> (r - VW_FNAME0) & 1)) {
			V->flive |= 1ull << (r - VW_FNAME0);
			return r;
		}
	vw_fail(e, "VIR: out of float names");
}

VPRIV void vw_tfree(ecx *e, int r)
{
	vwst *V = e->vw;

	if (r >= 0 && r < VW_FNAME0)
		V->live[r / 64] &= ~(1ull << (r % 64));
	else if (r >= VW_FNAME0 && r < VW_NONAME)
		V->flive &= ~(1ull << (r - VW_FNAME0));
}

VPRIV void vw_tkeep(ecx *e, int r)
{
	vwst *V = e->vw;

	if (r >= 0 && r < VW_FNAME0)
		V->live[r / 64] |= 1ull << (r % 64);
	else if (r >= VW_FNAME0 && r < VW_NONAME)
		V->flive |= 1ull << (r - VW_FNAME0);
}

VPRIV int vw_nfree(ecx *e)
{
	vwst *V = e->vw;

	return 192 - __builtin_popcountll(V->live[0]) - __builtin_popcountll(V->live[1]) - __builtin_popcountll(V->live[2]);
}

VPRIV int vw_nffree(ecx *e) { return 63 - __builtin_popcountll(e->vw->flive); }

VPRIV void vw_mov(ecx *e, int rd, int rs)
{
	if (rd == rs)
		return;
	vw_set(e, rd, vw_getraw(e, rs));
}

VPRIV int vw_toreg(ecx *e, val *v, int size)
{
	int r;

	(void)size;
	if (v->k == V_REG)
		return v->r;
	r = vw_treg(e);
	vw_set(e, r, vw_val(e, v, v->k == V_ADDR ? VT_PTR : VT_I64));
	*v = (val){ .k = V_REG, .r = (uint8_t)r };
	return r;
}

/* a result's name: a's if a is in an integer name, else a new one; b's freed */
static int vw_resname(ecx *e, const val *a, const val *b)
{
	int r = a->k == V_REG && a->r < VW_FNAME0 ? a->r : vw_treg(e);

	if (b && b->k == V_REG && b->r != r)
		vw_tfree(e, b->r);
	return r;
}

/* ---------------- slots and memory ---------------- */

VPRIV void vw_slot(ecx *e, int64_t off, uint64_t size, uint32_t align, int kind)
{
	vwst *V = e->vw;
	uint32_t n = 0;

	(void)kind;
	if (!size)
		size = 1;
	/* the new slot owns [off, off+size): earlier owners of an overlapping range are no longer current */
	for (uint32_t i = 0; i < V->nact; i++) {
		vwslot *s = &V->sl[V->act[i]];

		if (s->off < off + (int64_t)size && off < s->off + (int64_t)s->size)
			continue;
		V->act[n++] = V->act[i];
	}
	V->nact = n;
	GROW(V->sl, V->nsl, V->csl, 1);
	V->sl[V->nsl] = (vwslot){ off, size, align ? align : 1, 0, 0, 0, VW_NOBLK, 0 };
	GROW(V->act, V->nact, V->cact, 1);
	V->act[V->nact++] = V->nsl++;
	/* a fresh object: what the store table knew about these bytes is gone */
	for (uint32_t i = 0; i < V->nst; i++)
		if (V->st[i].off < off + (int64_t)size && off < V->st[i].off + V->st[i].size)
			V->st[i--] = V->st[--V->nst];
}

/* the slot that owns frame offset off now, and off's offset in it */
static uint32_t vw_slotat(ecx *e, int64_t off, uint64_t size, int64_t *rel)
{
	vwst *V = e->vw;

	for (uint32_t i = V->nact; i-- > 0;) {
		vwslot *s = &V->sl[V->act[i]];

		if (s->off <= off && off < s->off + (int64_t)s->size) {
			if (off + (int64_t)size > s->off + (int64_t)s->size)
				vw_fail(e, "VIR: an access across two slots");
			*rel = off - s->off;
			return V->act[i];
		}
	}
	vw_fail(e, "VIR: a frame access outside every slot");
}

static uint32_t vw_slotaddr(ecx *e, uint32_t s, int64_t rel)
{
	return vw_k(e, VO_SLOTADDR, VT_PTR, s, (uint32_t)rel, (uint32_t)((uint64_t)rel >> 32));
}

/*
 * A store through a pointer, a call or a barrier: what the store table knows of the
 * frame is gone.  Not only of slots whose address has been taken so far: the walk is
 * in text order, and a loop can run this code after an address taken further down
 * (x = i; *p = 1; s += x; p = &x;).  A slot whose address is never taken is promoted
 * afterwards (vw_promote, with the whole function's uses), so its loads go anyway.
 */
static void vw_st_clear(ecx *e, bool exposed_only)
{
	(void)exposed_only;
	e->vw->nst = 0;
}

/* the address of l (a ptr value); *slot: the slot (VW_NOBLK: not a frame access), *rel its offset */
static uint32_t vw_addr(ecx *e, const lval *l, uint64_t size, uint32_t *slot, int64_t *rel)
{
	*slot = VW_NOBLK;
	switch (l->k) {
	case L_FRAME:
		*slot = vw_slotat(e, l->off, size, rel);
		return vw_slotaddr(e, *slot, *rel);
	case L_SYM:
		note_ref(e, l->ref);
		return vw_k(e, VO_SYMADDR, VT_PTR, l->ref, (uint32_t)l->off, (uint32_t)((uint64_t)l->off >> 32));
	case L_REG: {
		uint32_t b = vw_get(e, l->r, VT_PTR);

		if (!l->off)
			return b;
		return vw_ins(e, VO_PTRADD, VT_PTR, 0, b, vw_iconst(e, VT_I64, l->off), 0);
	}
	default:
		vw_fail(e, "VIR: this lvalue kind");
	}
}

static uint32_t vw_log2(uint64_t x) { uint32_t k = 0; while (x > 1) x >>= 1, k++; return k; }

static uint32_t vw_mfl(int size, uint32_t align, bool sgn)
{
	uint32_t w = vw_log2((uint64_t)size), a = align ? vw_log2(align & -align) : 0;

	if (a > 15)
		a = 15;
	return w | (sgn ? VIF_MS : 0) | a << 4;
}

/* the variable of a local (L_VAR): its type */
static uint32_t vw_lvty(ecx *e, const lval *l) { return e->vw->vty[l->ref - VW_VAR0]; }

VPRIV void vw_load(ecx *e, int rd, const lval *l, int size, bool sgn)
{
	vwst *V = e->vw;
	bool f = rd >= VW_FNAME0;
	uint32_t ty, slot, a, v;
	int64_t rel;

	if (l->bf)
		vw_fail(e, "VIR: a bit-field lvalue");
	if (l->k == L_VAR) {
		v = vw_readv(e, l->ref, V->cur, vw_lvty(e, l));
		if (f)
			v = vw_conv(e, v, size == 4 ? VT_F32 : VT_F64);
		vw_set(e, rd, v);
		return;
	}
	if (f)
		ty = size == 4 ? VT_F32 : size == 8 ? VT_F64 : 0;
	else
		ty = size == 8 ? VT_I64 : sgn ? VT_I64 : VT_I32;
	if (!ty)
		vw_fail(e, "VIR: a float load of this size");
	a = vw_addr(e, l, (uint64_t)size, &slot, &rel);
	if (slot != VW_NOBLK && !e->novtag) {	/* the value last stored here, in this block (not volatile) */
		for (uint32_t i = V->nst; i-- > 0;) {
			vwstent *s = &V->st[i];

			if (s->slot == slot && s->off == l->off && s->size == size) {
				uint32_t x = s->v, xt = vw_ty(e, x);

				if (!f && size < 8 && xt != VT_F32 && xt != VT_F64) {	/* the low bytes, extended as a load would */
					x = vw_conv(e, x, size == 4 && !sgn ? VT_I32 : VT_I64);
					if (size < 4 || sgn)
						x = vw_ins(e, VO_EXT, vw_ty(e, x), vw_log2((uint64_t)size) | (sgn ? VIF_MS : 0), x, 0, 0);
				} else {
					x = vw_conv(e, x, ty);
				}
				vw_set(e, rd, x);
				return;
			}
		}
		V->sl[slot].loads++;
	}
	v = vw_ins(e, VO_LOAD, ty, vw_mfl(size, l->align, !f && sgn && size < 8) | (e->novtag ? VIF_VOL : 0), a, 0, 0);
	if (slot != VW_NOBLK)
		V->p[v].slot = slot + 1;	/* (which slot: for the dead-store pass) */
	vw_set(e, rd, v);
}

/* value v stored as size bytes: the type a store of it has */
static uint32_t vw_stty(ecx *e, uint32_t v, int size)
{
	uint32_t t = vw_ty(e, v);

	if (t == VT_F32 || t == VT_F64)
		return size == 4 ? VT_F32 : size == 8 ? VT_F64 : 0;
	if (size == 8)
		return t == VT_PTR ? VT_PTR : VT_I64;
	return VT_I32;
}

VPRIV void vw_store(ecx *e, int rs, const lval *l, int size)
{
	vwst *V = e->vw;
	uint32_t v = vw_getraw(e, rs), ty, slot, a;
	int64_t rel;

	if (rs >= VW_FNAME0 && rs < VW_NONAME)
		V->fprod[rs - VW_FNAME0] = 0;	/* (an assignment's value is not the product any more, as clang) */
	if (l->bf)
		vw_fail(e, "VIR: a bit-field lvalue");
	if (l->k == L_VAR) {
		uint32_t lt = vw_lvty(e, l);

		if (lt == VT_F32 || lt == VT_F64)
			v = vw_conv(e, v, lt);
		else {
			v = vw_conv(e, v, size == 8 ? (lt == VT_PTR ? VT_PTR : VT_I64) : VT_I32);
			if (size < 4)	/* the local's value is always its type's (normalized) */
				v = vw_ins(e, VO_EXT, VT_I32, vw_log2((uint64_t)size) | (V->vsgn[l->ref - VW_VAR0] ? VIF_MS : 0), v, 0, 0);
			v = vw_conv(e, v, lt);
		}
		vw_write(e, l->ref, V->cur, v);
		return;
	}
	ty = vw_stty(e, v, size);
	if (!ty) {
		if (size == 8 && vw_ty(e, v) == VT_F32)
			ty = VT_F32, size = 4;	/* (a float's word in an argument slot: its low 4 bytes) */
		else
			vw_fail(e, "VIR: a float store of this size");
	}
	a = vw_addr(e, l, (uint64_t)size, &slot, &rel);
	v = vw_conv(e, v, ty);
	{
		uint32_t s = vw_ins(e, VO_STORE, VT_VOID, vw_mfl(size, l->align, false) | (e->novtag ? VIF_VOL : 0), a, v, 0);

		if (slot != VW_NOBLK)
			V->p[s].slot = slot + 1;
	}
	if (slot != VW_NOBLK) {
		/* the bytes it covers are this value now */
		for (uint32_t i = 0; i < V->nst; i++)
			if (V->st[i].slot == slot && V->st[i].off < l->off + size && l->off < V->st[i].off + V->st[i].size)
				V->st[i--] = V->st[--V->nst];
		GROW(V->st, V->nst, V->cst, 1);
		V->st[V->nst++] = (vwstent){ slot, l->off, size, v };
	} else {
		vw_st_clear(e, true);	/* through a pointer: any exposed slot */
	}
}

VPRIV int vw_laddr(ecx *e, const lval *l)
{
	vwst *V = e->vw;
	uint32_t slot, a;
	int64_t rel;
	int r;

	if (l->k == L_VAR || l->k == L_VREG)
		vw_fail(e, "VIR: the address of a register variable");
	a = vw_addr(e, l, 1, &slot, &rel);
	if (slot != VW_NOBLK) {
		V->sl[slot].exposed = 1;
		vw_st_clear(e, true);
	}
	r = l->k == L_REG ? l->r : vw_treg(e);
	vw_set(e, r, a);
	return r;
}

VPRIV void vw_copy(ecx *e, int rdst, int rsrc, uint64_t n)
{
	uint32_t d = vw_get(e, rdst, VT_PTR), s = vw_get(e, rsrc, VT_PTR);

	if (n == 1 || n == 2 || n == 4 || n == 8) {	/* one load and store (memcpy(&u, &d, 8)): promotable */
		uint32_t fl = vw_mfl((int)n, 1, false);

		vw_ins(e, VO_STORE, VT_VOID, fl, d, vw_ins(e, VO_LOAD, n == 8 ? VT_I64 : VT_I32, fl, s, 0, 0), 0);
	} else {
		vw_ins(e, VO_MEMCPY, VT_VOID, 0, d, s, vw_iconst(e, VT_I64, (int64_t)n));
	}
	vw_st_clear(e, true);
}

VPRIV void vw_zero(ecx *e, int rdst, uint64_t n)
{
	uint32_t d = vw_get(e, rdst, VT_PTR);

	vw_ins(e, VO_MEMSET, VT_VOID, 0, d, vw_iconst(e, VT_I32, 0), vw_iconst(e, VT_I64, (int64_t)n));
	vw_st_clear(e, true);
}

/* ---------------- integer operations ---------------- */

VPRIV val vw_umulhi(ecx *e, val a, val b)
{
	uint32_t x = vw_val(e, &a, VT_I64), y = vw_val(e, &b, VT_I64);
	int r = vw_resname(e, &a, &b);

	vw_set(e, r, vw_ins(e, VO_UMULHI, VT_I64, 0, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_smulhi(ecx *e, val a, val b)
{
	uint32_t x = vw_val(e, &a, VT_I64), y = vw_val(e, &b, VT_I64);
	int r = vw_resname(e, &a, &b);

	vw_set(e, r, vw_ins(e, VO_SMULHI, VT_I64, 0, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_bin(ecx *e, int op, int size, bool sgn, val a, val b)
{
	uint32_t ty = vw_ity(size), vop, x, y, at, bt;
	int r;

	at = vw_vty(e, &a), bt = vw_vty(e, &b);
	if (size == 8 && (op == T_PLUS || op == T_MINUS) && (at == VT_PTR || bt == VT_PTR)) {
		/* pointer arithmetic */
		if (op == T_PLUS && at != VT_PTR) {
			val t = a;

			a = b, b = t;
			at = VT_PTR, bt = vw_vty(e, &b);
		}
		if (at == VT_PTR && !(op == T_MINUS && bt == VT_PTR)) {
			x = vw_val(e, &a, VT_PTR);
			y = vw_val(e, &b, VT_I64);
			if (op == T_MINUS)
				y = b.k == V_CONST ? vw_iconst(e, VT_I64, -(int64_t)b.i) : vw_ins(e, VO_NEG, VT_I64, 0, y, 0, 0);
			r = vw_resname(e, &a, &b);
			vw_set(e, r, vw_ins(e, VO_PTRADD, VT_PTR, 0, x, y, 0));
			return (val){ .k = V_REG, .r = (uint8_t)r };
		}
		if (op == T_MINUS && at == VT_PTR && bt == VT_PTR) {
			x = vw_val(e, &a, VT_PTR);
			y = vw_val(e, &b, VT_PTR);
			r = vw_resname(e, &a, &b);
			vw_set(e, r, vw_ins(e, VO_PSUB, VT_I64, 0, x, y, 0));
			return (val){ .k = V_REG, .r = (uint8_t)r };
		}
	}
	switch (op) {
	case T_PLUS: vop = VO_ADD; break;
	case T_MINUS: vop = VO_SUB; break;
	case T_STAR: vop = VO_MUL; break;
	case T_SLASH: vop = sgn ? VO_SDIV : VO_UDIV; break;
	case T_PERCENT: vop = sgn ? VO_SREM : VO_UREM; break;
	case T_AMP: vop = VO_AND; break;
	case T_PIPE: vop = VO_OR; break;
	case T_CARET: vop = VO_XOR; break;
	case T_LESSLESS: vop = VO_SHL; break;
	case T_GREATERGREATER: vop = sgn ? VO_ASHR : VO_LSHR; break;
	default: vw_fail(e, "VIR: this operator");
	}
	if ((vop == VO_SHL || vop == VO_ASHR || vop == VO_LSHR) && b.k == V_CONST)
		b.i &= size * 8 - 1;
	if (a.k == V_CONST && b.k == V_CONST && vop != VO_SDIV && vop != VO_UDIV && vop != VO_SREM && vop != VO_UREM) {
		/* (the walker folds; what reaches here is rare) */
	}
	x = vw_val(e, &a, ty);
	y = vw_val(e, &b, ty);
	r = vw_resname(e, &a, &b);
	vw_set(e, r, vw_ins(e, vop, ty, 0, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_un(ecx *e, int op, int size, val a)
{
	uint32_t ty = vw_ity(size), x = vw_val(e, &a, ty);
	int r = vw_resname(e, &a, NULL);

	vw_set(e, r, vw_ins(e, op == T_MINUS ? VO_NEG : VO_NOT, ty, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV void vw_ext(ecx *e, int r, int from, bool sgn)
{
	uint32_t v;

	if (from >= 8)
		return;
	v = vw_getraw(e, r);
	if (e->vw->p[v].in.op == VO_ICONST) {	/* a constant: folded */
		uint64_t x = e->vw->p[v].in.b | (uint64_t)e->vw->p[v].in.c << 32;
		int sh = 64 - 8 * from;

		x = sgn ? (uint64_t)((int64_t)(x << sh) >> sh) : x << sh >> sh;
		vw_set(e, r, vw_iconst(e, !sgn && from == 4 ? VT_I32 : VT_I64, (int64_t)x));
		return;
	}
	if (from == 4 && !sgn) {
		vw_set(e, r, vw_conv(e, v, VT_I32));
		return;
	}
	if (vw_ty(e, v) == VT_PTR)
		v = vw_conv(e, v, VT_I64);
	if (vw_ty(e, v) == VT_I32 && from == 4) {	/* sign extension of an i32 */
		vw_set(e, r, vw_ins(e, VO_EXT, VT_I64, 2 | VIF_MS, v, 0, 0));
		return;
	}
	if (vw_ty(e, v) == VT_F32 || vw_ty(e, v) == VT_F64)
		vw_fail(e, "VIR: extension of a float");
	/* narrow: the result as wide as the operand (an i32 with a zero upper half, or i64 for a sign) */
	if (sgn)
		v = vw_conv(e, v, VT_I64);
	vw_set(e, r, vw_ins(e, VO_EXT, vw_ty(e, v), vw_log2((uint64_t)from) | (sgn ? VIF_MS : 0), v, 0, 0));
}

static uint32_t vw_icc(int op, bool sgn)
{
	switch (op) {
	case T_EQUALEQUAL: return 0;
	case T_EXCLAIMEQ: return 1;
	case T_LESS: return sgn ? 2 : 6;
	case T_LESSEQ: return sgn ? 3 : 7;
	case T_GREATER: return sgn ? 4 : 8;
	default: return sgn ? 5 : 9;
	}
}

static uint32_t vw_icmp(ecx *e, int op, int size, bool sgn, val *a, val *b)
{
	uint32_t ty = vw_ity(size), x, y;

	if (size == 8 && !sgn && vw_vty(e, a) == VT_PTR && vw_vty(e, b) == VT_PTR)
		ty = VT_PTR;
	x = vw_val(e, a, ty);
	y = vw_val(e, b, ty);
	return vw_ins(e, VO_ICMP, VT_I32, vw_icc(op, sgn), x, y, 0);
}

VPRIV val vw_cmp(ecx *e, int op, int size, bool sgn, val a, val b)
{
	uint32_t c = vw_icmp(e, op, size, sgn, &a, &b);
	int r = vw_resname(e, &a, &b);

	vw_set(e, r, c);
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* to lbl if c (an i32 or i64) is nonzero, else on */
static void vw_brif(ecx *e, uint32_t c, uint32_t lbl, bool nz)
{
	vwst *V = e->vw;
	uint32_t t = vw_lblk(e, lbl), f = vw_newblk(e);

	V->b[V->cur].term = vw_put(e, VO_BRIF, VT_VOID, 0, c, nz ? t : f, nz ? f : t, V->cur);
	V->cur = f;
	/* (the fallthrough's only predecessor is this block: the store table stays valid) */
	V->b[f].fall = 1;
}

VPRIV void vw_cmpbr(ecx *e, int op, int size, bool sgn, val a, val b, uint32_t lbl)
{
	uint32_t c = vw_icmp(e, op, size, sgn, &a, &b);

	if (a.k == V_REG) vw_tfree(e, a.r);
	if (b.k == V_REG) vw_tfree(e, b.r);
	vw_brif(e, c, lbl, true);
}

VPRIV void vw_brz(ecx *e, val a, int size, bool nz, uint32_t lbl)
{
	uint32_t t = vw_vty(e, &a), c;

	if (t == VT_PTR)
		c = vw_ins(e, VO_ICMP, VT_I32, 1, vw_val(e, &a, VT_PTR), vw_iconst(e, VT_PTR, 0), 0);
	else
		c = vw_val(e, &a, vw_ity(size));
	if (a.k == V_REG) vw_tfree(e, a.r);
	vw_brif(e, c, lbl, nz);
}

VPRIV void vw_switch_bitset(ecx *e, int r, int size, const em_switch_bitset_recipe *recipe, uint32_t target, uint32_t dflt)
{
	(void)r, (void)size, (void)recipe, (void)target, (void)dflt;
	vw_fail(e, "VIR: switch bit set");
}

static int vw_casecmp(const void *a, const void *b)
{
	const uint64_t *x = a, *y = b;

	return x[0] < y[0] ? -1 : x[0] > y[0];
}

VPRIV void vw_switch_table(ecx *e, int r, int size, const em_switch_case *cases, uint32_t n, uint32_t dflt)
{
	vwst *V = e->vw;
	uint32_t ty = vw_ity(size), x = vw_get(e, r, ty), st = size == 8 ? 5 : 3, m = 0;
	uint64_t mask = size == 8 ? ~0ull : 0xffffffffull, *c3 = malloc((size_t)(2 * n + 1) * 3 * sizeof *c3);
	uint32_t o;

	if (!c3)
		vw_fail(e, "out of memory");
	/* VIR's cases are unsigned and sorted: a range across the sign splits in two */
	for (uint32_t k = 0; k < n; k++) {
		uint64_t lo = (uint64_t)cases[k].lo & mask, hi = (uint64_t)cases[k].hi & mask, t = vw_lblk(e, cases[k].target);

		if (lo > hi) {
			c3[3 * m] = lo, c3[3 * m + 1] = mask, c3[3 * m + 2] = t, m++;
			lo = 0;
		}
		c3[3 * m] = lo, c3[3 * m + 1] = hi, c3[3 * m + 2] = t, m++;
	}
	qsort(c3, m, 3 * sizeof *c3, vw_casecmp);
	o = vw_xput(e, &m, 1);
	GROW(V->x, V->nx, V->cx, m * st);
	for (uint32_t k = 0; k < m; k++) {
		uint64_t lo = c3[3 * k], hi = c3[3 * k + 1];
		uint32_t *w = &V->x[V->nx];

		if (size == 8) {
			w[0] = (uint32_t)lo, w[1] = (uint32_t)(lo >> 32), w[2] = (uint32_t)hi, w[3] = (uint32_t)(hi >> 32);
		} else {
			w[0] = (uint32_t)lo, w[1] = (uint32_t)hi;
		}
		w[st - 1] = (uint32_t)c3[3 * k + 2];
		V->nx += st;
	}
	free(c3);
	vw_term(e, VO_SWITCH, x, vw_lblk(e, dflt), o, 0);
	e->reach = false;
}

/* __int128 and _BitInt: not in 1.0 (SPEC 19) */
VPRIV bool vw_ldmem(void) { return true; }
VPRIV bool vw_i128mem(void) { return true; }
#define VW_NO(name, ...) VPRIV val vw_##name(ecx *e, __VA_ARGS__) { vw_fail(e, "VIR: " #name); }
VPRIV val vw_wconst(ecx *e, i128 v) { (void)v; vw_fail(e, "VIR: __int128"); }
VPRIV val vw_wbitint(ecx *e, val v, unsigned bits, bool sgn) { (void)v, (void)bits, (void)sgn; vw_fail(e, "VIR: _BitInt"); }
VPRIV val vw_bigconst(ecx *e, qty t, const uint32_t *words, i128 small) { (void)t, (void)words, (void)small; vw_fail(e, "VIR: _BitInt"); }
VPRIV val vw_bigbin(ecx *e, int op, qty t, val a, val b) { (void)op, (void)t, (void)a, (void)b; vw_fail(e, "VIR: _BitInt"); }
VPRIV val vw_bigcmp(ecx *e, int op, qty t, val a, val b) { (void)op, (void)t, (void)a, (void)b; vw_fail(e, "VIR: _BitInt"); }
VPRIV val vw_bigcvt(ecx *e, val v, qty from, qty to) { (void)v, (void)from, (void)to; vw_fail(e, "VIR: _BitInt"); }
VPRIV val vw_wbin(ecx *e, int op, bool sgn, val a, val b) { (void)op, (void)sgn, (void)a, (void)b; vw_fail(e, "VIR: __int128"); }
VPRIV val vw_wun(ecx *e, int op, val a) { (void)op, (void)a; vw_fail(e, "VIR: __int128"); }
VPRIV val vw_wcmp(ecx *e, int op, bool sgn, val a, val b) { (void)op, (void)sgn, (void)a, (void)b; vw_fail(e, "VIR: __int128"); }
VPRIV val vw_wcvt(ecx *e, val v, qty from, qty to) { (void)v, (void)from, (void)to; vw_fail(e, "VIR: __int128"); }

/* ---------------- floating point ---------------- */

static uint32_t vw_fty(ecx *e, qty t)
{
	int k = KIND(e->c, t);

	if (k == TY_FLOAT)
		return VT_F32;
	if (k == TY_DOUBLE || (k == TY_LDOUBLE && tsize(e->c, t) == 8))
		return VT_F64;
	vw_fail(e, "VIR: this floating type");
}

static int vw_fres(ecx *e, const val *a, const val *b)
{
	int r = a->k == V_REG && a->r >= VW_FNAME0 ? a->r : vw_ftreg(e);

	if (b && b->k == V_REG && b->r != r)
		vw_tfree(e, b->r);
	return r;
}

VPRIV val vw_fconst(ecx *e, uint64_t bits, int size)
{
	int r = vw_ftreg(e);

	vw_set(e, r, vw_k(e, VO_FCONST, size == 4 ? VT_F32 : VT_F64, 0, (uint32_t)bits, size == 4 ? 0 : (uint32_t)(bits >> 32)));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* a is the product an expression just made (in its name still), or that negated: the fmul, else 0.
 * *fl: VIF_NEGP when negated. */
static uint32_t vw_fprod(ecx *e, const val *a, uint32_t x, uint32_t ty, uint32_t *fl)
{
	vwst *V = e->vw;
	uint32_t m = x;

	*fl = 0;
	/* (arm64 only: the one lowering with fmadd) */
	if (e->c->o->no_fp_contract || e->c->o->target > PP_DARWIN || a->k != V_REG || a->r < VW_FNAME0 ||
	    V->fprod[a->r - VW_FNAME0] != x || !x)
		return 0;
	if (V->p[m].in.op == VO_FNEG)
		*fl |= VIF_NEGP, m = V->p[m].in.a;
	if (V->p[m].in.op != VO_FMUL || V->p[m].in.ty != ty)
		return 0;
	if (V->p[V->p[m].in.a].in.op == VO_FCONST && V->p[V->p[m].in.b].in.op == VO_FCONST)
		return 0;	/* (a constant product is rounded on its own) */
	return m;
}

VPRIV val vw_fbin(ecx *e, int op, qty t, val a, val b)
{
	uint32_t ty = vw_fty(e, t), x = vw_val(e, &a, ty), y = vw_val(e, &b, ty);
	int r = vw_fres(e, &a, &b);
	uint32_t vop = op == T_PLUS ? VO_FADD : op == T_MINUS ? VO_FSUB : op == T_STAR ? VO_FMUL : VO_FDIV;
	uint32_t v, m = 0, fl = 0;
	bool left = true;

	if (vop <= VO_FSUB && !(m = vw_fprod(e, &a, x, ty, &fl)))
		left = false, m = vw_fprod(e, &b, y, ty, &fl);
	if (m) {
		/* a product this expression made, added or subtracted: one fused operation, as clang's
		 * -ffp-contract=on (the left operand first; the product itself goes when nothing else reads it) */
		const vwp *P = e->vw->p;
		uint32_t pa = P[m].in.a, pb = P[m].in.b, c = left ? y : x;

		if (vop == VO_FSUB)
			fl ^= left ? VIF_NEGC : VIF_NEGP;
		/* a negated factor or addend is a flag (exact) */
		if (P[pa].in.op == VO_FNEG)
			fl ^= VIF_NEGP, pa = P[pa].in.a;
		if (P[pb].in.op == VO_FNEG)
			fl ^= VIF_NEGP, pb = P[pb].in.a;
		if (P[c].in.op == VO_FNEG)
			fl ^= VIF_NEGC, c = P[c].in.a;
		v = vw_ins(e, VO_FMA, ty, fl, pa, pb, c);
	} else {
		v = vw_ins(e, vop, ty, 0, x, y, 0);
	}
	vw_set(e, r, v);
	if (vop == VO_FMUL)
		e->vw->fprod[r - VW_FNAME0] = v;
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_fneg(ecx *e, qty t, val a)
{
	uint32_t ty = vw_fty(e, t), x = vw_val(e, &a, ty);
	int r = vw_fres(e, &a, NULL);

	bool prod = a.k == V_REG && a.r >= VW_FNAME0 && x && e->vw->fprod[a.r - VW_FNAME0] == x && e->vw->p[x].in.op == VO_FMUL;
	uint32_t v = vw_ins(e, VO_FNEG, ty, 0, x, 0, 0);

	vw_set(e, r, v);
	if (prod)
		e->vw->fprod[r - VW_FNAME0] = v;
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_fsqrt(ecx *e, qty t, val a)
{
	uint32_t ty = vw_fty(e, t), x = vw_val(e, &a, ty);
	int r = vw_fres(e, &a, NULL);

	vw_set(e, r, vw_ins(e, VO_FSQRT, ty, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_root4(ecx *e, val a) { (void)a; vw_fail(e, "VIR: root4"); }
VPRIV val vw_pcmpstr(ecx *e, uint32_t n) { (void)n; vw_fail(e, "VIR: pcmpstr"); }

VPRIV val vw_fcmp(ecx *e, int op, qty t, val a, val b)
{
	uint32_t ty = vw_fty(e, t), x = vw_val(e, &a, ty), y = vw_val(e, &b, ty), cc;
	int r;

	switch (op) {
	case T_EQUALEQUAL: cc = 0; break;
	case T_EXCLAIMEQ: cc = 1; break;
	case T_LESS: cc = 2; break;
	case T_LESSEQ: cc = 3; break;
	case T_GREATER: cc = 4; break;
	case T_GREATEREQ: cc = 5; break;
	case WK_FCMP_UNORDERED: cc = 6; break;
	case WK_FCMP_ORDERED_NE: cc = 7; break;
	default: vw_fail(e, "VIR: this float comparison");
	}
	if (a.k == V_REG) vw_tfree(e, a.r);
	if (b.k == V_REG) vw_tfree(e, b.r);
	r = vw_treg(e);
	vw_set(e, r, vw_ins(e, VO_FCMP, VT_I32, cc, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_cvt(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool ff = is_flt(c, from), tf = is_flt(c, to);
	int fs = wk_size(c, from), ts = wk_size(c, to);
	uint32_t x;
	int r;

	if (ff && tf) {
		uint32_t a = vw_fty(e, from), b = vw_fty(e, to);

		x = vw_val(e, &v, a);
		if (a != b)
			x = vw_ins(e, a == VT_F32 ? VO_FEXT : VO_FTRUNC, b, 0, x, 0, 0);
		r = vw_fres(e, &v, NULL);
		vw_set(e, r, x);
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
	if (tf) {	/* integer to float */
		bool sg = wk_signed(c, from);
		uint32_t b = vw_fty(e, to), it = fs == 8 ? VT_I64 : VT_I32;

		x = vw_val(e, &v, fs == 8 ? VT_I64 : (fs < 4 || sg) ? VT_I64 : VT_I32);
		if (fs < 8 && sg) {	/* the low fs bytes, signed: as i32 */
			x = vw_ins(e, VO_EXT, VT_I64, vw_log2((uint64_t)fs) | VIF_MS, x, 0, 0);
			x = vw_conv(e, x, VT_I32);
			it = VT_I32;
		} else if (fs < 4) {
			x = vw_conv(e, vw_ins(e, VO_EXT, VT_I64, vw_log2((uint64_t)fs), x, 0, 0), VT_I32);
			it = VT_I32;
		}
		(void)it;
		if (v.k == V_REG && v.r < VW_FNAME0)
			vw_tfree(e, v.r);
		r = vw_ftreg(e);
		vw_set(e, r, vw_ins(e, sg ? VO_S2F : VO_U2F, b, 0, x, 0, 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
	if (KIND(c, to) == TY_BOOL) {
		uint32_t a = vw_fty(e, from);

		x = vw_val(e, &v, a);
		if (v.k == V_REG) vw_tfree(e, v.r);
		r = vw_treg(e);
		vw_set(e, r, vw_ins(e, VO_FCMP, VT_I32, 1, x, vw_k(e, VO_FCONST, a, 0, 0, 0), 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
	{	/* float to integer */
		uint32_t a = vw_fty(e, from), it = ts == 8 ? VT_I64 : VT_I32;
		bool sg = wk_signed(c, to);

		x = vw_val(e, &v, a);
		if (v.k == V_REG) vw_tfree(e, v.r);
		r = vw_treg(e);
		x = vw_ins(e, sg ? VO_F2S : VO_F2U, it, 0, x, 0, 0);
		vw_set(e, r, x);
		if (ts < 4)
			vw_ext(e, r, ts, sg);
		else if (ts == 4 && sg)
			vw_ext(e, r, 4, true);
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
}

VPRIV val vw_fbits(ecx *e, val v, int size)
{
	uint32_t x = vw_val(e, &v, size == 4 ? VT_F32 : VT_F64);
	int r;

	if (v.k == V_REG) vw_tfree(e, v.r);
	r = vw_treg(e);
	vw_set(e, r, vw_ins(e, VO_BITCAST, size == 4 ? VT_I32 : VT_I64, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* ---------------- builtins and the rest ---------------- */

VPRIV val vw_bitop(ecx *e, int bi, int size, val a)
{
	uint32_t ty = vw_ity(size), x = vw_val(e, &a, ty), op;
	int r = vw_resname(e, &a, NULL);

	switch (bi) {
	case BI_CLZ: case BI_CLZL: case BI_CLZLL: op = VO_CLZ; break;
	case BI_CTZ: case BI_CTZL: case BI_CTZLL: op = VO_CTZ; break;
	case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL: op = VO_POPCNT; break;
	case BI_BSWAP32: case BI_BSWAP64: op = VO_BSWAP; break;
	case BI_BSWAP16:
		x = vw_ins(e, VO_BSWAP, VT_I32, 0, vw_conv(e, x, VT_I32), 0, 0);
		vw_set(e, r, vw_ins(e, VO_LSHR, VT_I32, 0, x, vw_iconst(e, VT_I32, 16), 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
		x = vw_ins(e, VO_POPCNT, ty, 0, x, 0, 0);
		vw_set(e, r, vw_ins(e, VO_AND, ty, 0, x, vw_iconst(e, ty, 1), 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	default: vw_fail(e, "VIR: this builtin");
	}
	vw_set(e, r, vw_ins(e, op, ty, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* sequentially consistent; a result is zero-extended (an i32 for 4 bytes or less) */
VPRIV int vw_atomic(ecx *e, int op, int size, int rp, int rv, int rexp)
{
	uint32_t ty = vw_ity(size), fl = vw_mfl(size, (uint32_t)size, false), p, v = 0;
	int r;

	if (op == WK_AFENCE) {
		vw_ins(e, VO_AFENCE, VT_VOID, 0, 0, 0, 0);
		vw_st_clear(e, true);
		return -1;
	}
	p = vw_get(e, rp, VT_PTR);
	if (op != WK_ALOAD)
		v = vw_get(e, rv, ty);
	vw_st_clear(e, true);
	switch (op) {
	case WK_ALOAD: v = vw_ins(e, VO_ALOAD, ty, fl, p, 0, 0); break;
	case WK_ASTORE: vw_ins(e, VO_ASTORE, VT_VOID, fl, p, v, 0); return -1;
	case WK_AXCHG: v = vw_ins(e, VO_AXCHG, ty, fl, p, v, 0); break;
	case WK_AXADD: v = vw_ins(e, VO_AXADD, ty, fl, p, v, 0); break;
	default: v = vw_ins(e, VO_ACAS, ty, fl, p, vw_get(e, rexp, ty), v); break;
	}
	r = vw_treg(e);
	vw_set(e, r, v);
	if (size < 4)	/* (the narrow value zero-extended, as the hardware's) */
		vw_ext(e, r, size, false);
	return r;
}

VPRIV int vw_tlsaddr(ecx *e, uint32_t ref, int model) { (void)ref, (void)model; vw_fail(e, "VIR: thread-local"); }
VPRIV val vw_frame(ecx *e, bool ret, uint32_t level) { (void)ret, (void)level; vw_fail(e, "VIR: frame address"); }
VPRIV val vw_alloca(ecx *e, val n) { (void)n; vw_fail(e, "VIR: alloca"); }
VPRIV int vw_spget(ecx *e) { vw_fail(e, "VIR: stack pointer"); }
VPRIV void vw_spset(ecx *e, int r) { (void)r; vw_fail(e, "VIR: stack pointer"); }
VPRIV val vw_setjmp(ecx *e, int rbuf) { (void)rbuf; vw_fail(e, "VIR: setjmp"); }
VPRIV void vw_longjmp(ecx *e, int rbuf) { (void)rbuf; vw_fail(e, "VIR: longjmp"); }
VPRIV void vw_asm(ecx *e, uint32_t n) { (void)n; vw_fail(e, "VIR: inline asm"); }
/* the va_list at rap points at the first unnamed argument (Apple's: on the stack) */
VPRIV void vw_va_start(ecx *e, int rap)
{
	if (!DARWIN(e->c))
		vw_fail(e, "VIR: va_start (AAPCS64's register save areas)");
	vw_ins(e, VO_VA_START, VT_VOID, 0, vw_get(e, rap, VT_PTR), 0, 0);
	vw_st_clear(e, true);
}
/* the address of the next argument of va_list *rap, which moves past it (as va_arg_ in
 * arch/a64/emit/builtin.c); Apple's va_list is a pointer into the stack arguments,
 * 8 bytes each (16-aligned ones aligned first, records over 16 bytes by reference) */
VPRIV int vw_va_arg(ecx *e, int rap, qty t)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	bool big = is_recabi(c, t) && size > 16;
	lval ap = { .k = L_REG, .r = (uint8_t)rap, .align = 8 };
	uint32_t p, q;
	int r, rn;

	if (!DARWIN(c))
		vw_fail(e, "VIR: va_arg (AAPCS64's register save areas)");
	r = vw_treg(e);
	vw_load(e, r, &ap, 8, false);
	p = vw_get(e, r, VT_PTR);
	if (size && !big && abi_align(c, t) == 16) {
		q = vw_ins(e, VO_P2I, VT_I64, 0, p, 0, 0);
		q = vw_ins(e, VO_ADD, VT_I64, 0, q, vw_iconst(e, VT_I64, 15), 0);
		q = vw_ins(e, VO_AND, VT_I64, 0, q, vw_iconst(e, VT_I64, -16), 0);
		p = vw_ins(e, VO_I2P, VT_PTR, 0, q, 0, 0);
	}
	q = vw_ins(e, VO_PTRADD, VT_PTR, 0, p, vw_iconst(e, VT_I64, big ? 8 : (int64_t)((size + 7) & ~(uint64_t)7)), 0);
	rn = vw_treg(e);
	vw_set(e, rn, q);
	vw_store(e, rn, &ap, 8);
	vw_tfree(e, rn);
	vw_set(e, r, p);
	if (big) {
		lval m = { .k = L_REG, .r = (uint8_t)r, .align = 8 };

		vw_load(e, r, &m, 8, false);
	}
	return r;
}
VPRIV void vw_retrec(ecx *e, int raddr, const wkarg *r, uint32_t lbl) { (void)raddr, (void)r, (void)lbl; vw_fail(e, "VIR: a record result"); }
VPRIV bool vw_tail_return(ecx *e, uint32_t expr) { (void)e, (void)expr; return false; }

VPRIV void vw_trap(ecx *e)
{
	vw_term(e, VO_TRAP, 0, 0, 0, 0);
}

VPRIV void vw_barrier(ecx *e)
{
	vw_ins(e, VO_BARRIER, VT_VOID, 0, 0, 0, 0);
	vw_st_clear(e, true);
}

VPRIV void vw_dbgrow(ecx *e, uint32_t loc) { e->curloc = loc; }

VPRIV void vw_label(ecx *e, uint32_t l)
{
	vwst *V = e->vw;
	uint32_t b = vw_lblk(e, l);

	e->reach = true;
	V->b[V->cur].term = vw_put(e, VO_BR, VT_VOID, 0, b, 0, 0, V->cur);
	V->cur = b;
	V->nst = 0;
	wk_ael_clear(e, UINT32_MAX);
}

VPRIV void vw_jump(ecx *e, uint32_t l) { vw_br(e, vw_lblk(e, l)); }
VPRIV void vw_jumpr(ecx *e, int r) { (void)r; vw_fail(e, "VIR: computed goto"); }
VPRIV void vw_named(ecx *e, uint32_t ref) { (void)ref; vw_fail(e, "VIR: an address-taken label"); }

/* ---------------- locals ---------------- */

VPRIV void vw_locals_begin(ecx *e, uint32_t s) { (void)e, (void)s; }

/* every eligible scalar local is a variable, keyed by (symbol, inline copy) */
VPRIV bool vw_local(ecx *e, uint32_t s, lval *l)
{
	vwst *V = e->vw;
	cc *c = e->c;
	qty t = c->syms[s].ty;
	uint32_t ty, inst = e->wk->inst;

	if (!em_local_eligible(e, s))
		return false;
	if (is_ptr(c, t))
		ty = VT_PTR;
	else if (is_integer(c, t) && !is_wide(c, t) && tsize(c, t) - 1 < 8)
		ty = tsize(c, t) == 8 ? VT_I64 : VT_I32;
	else if (KIND(c, t) == TY_FLOAT)
		ty = VT_F32;
	else if (KIND(c, t) == TY_DOUBLE)
		ty = VT_F64;
	else
		return false;
	for (uint32_t i = 0; i < V->nloc; i++)	/* (few locals; a hash once it matters) */
		if (V->loc[i].sym == s && V->loc[i].inst == inst) {
			*l = (lval){ .k = L_VAR, .ref = VW_VAR0 + i, .align = sym_align(c, s) };
			return true;
		}
	GROW(V->loc, V->nloc, V->cloc, 1);
	GROW(V->vty, V->nloc, V->cvty, 1);
	GROW(V->vsgn, V->nloc, V->cvsgn, 1);
	V->loc[V->nloc] = (vwloc){ s, inst };
	V->vty[V->nloc] = (uint8_t)ty;
	V->vsgn[V->nloc] = is_integer(c, t) && is_signed(c, t);
	*l = (lval){ .k = L_VAR, .ref = VW_VAR0 + V->nloc, .align = sym_align(c, s) };
	V->nloc++;
	return true;
}

/* a local declared without an initializer: a value from an earlier pass through its scope
 * (a loop's) is not its value, so its variable or slot gets a definition here and a promoted
 * local is not carried around the loop.  (The value is 0: vw_undef.)  In a slot, a memset
 * marked VW_LIFETIME: promotion takes it as zero stores, emission drops it. */
VPRIV void vw_undecl(ecx *e, const lval *l, uint64_t size)
{
	vwst *V = e->vw;
	uint32_t slot, a, m;
	int64_t rel;

	if (l->k == L_VAR) {
		vw_write(e, l->ref, V->cur, vw_undef(e, vw_lvty(e, l)));
		return;
	}
	if (l->k != L_FRAME || !size || size > INT32_MAX)
		return;
	a = vw_addr(e, l, size, &slot, &rel);
	if (slot == VW_NOBLK)
		return;
	m = vw_ins(e, VO_MEMSET, VT_VOID, VW_LIFETIME, a, vw_iconst(e, VT_I32, 0), vw_iconst(e, VT_I64, (int64_t)size));
	V->p[m].slot = slot + 1;
	for (uint32_t i = 0; i < V->nst; i++)	/* (what store forwarding knew of these bytes) */
		if (V->st[i].slot == slot && V->st[i].off < l->off + (int64_t)size && l->off < V->st[i].off + V->st[i].size)
			V->st[i--] = V->st[--V->nst];
}

/* ---------------- calls ---------------- */

/* the classification for arm64 (the signature's groups): scalars only for now */
VPRIV void vw_classify(ecx *e, qty t, wkarg *a, bool unnamed)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	int k = KIND(c, t);

	memset(a, 0, sizeof *a);
	a->size = size;
	a->align = talign(c, t) > 8 ? talign(c, t) : 8;
	a->unnamed = unnamed;
	if (is_record(c, t) || is_array(c, t) || is_vec(c, t) || is_wide(c, t) || k == TY_LDOUBLE || k == TY_FLOAT128 ||
	    k == TY_FLOAT16 || k == TY_BFLOAT16 || size > 8)
		vw_fail(e, "VIR: an argument or result passed as a record");
	a->n = 1;
	a->cls[0] = a->cls[1] = is_flt(c, t) ? WA_SSE : WA_INT;
}

static uint32_t vw_group(const wkarg *a, uint32_t *w)
{
	uint32_t pc = a->cls[0] == WA_SSE ? (a->size == 4 ? VC_F32 : VC_F64) : VC_INT;

	w[0] = VG_WORD0(VG_REG, 1, pc, 0, 0, 0, a->unnamed ? VG_UNNAMED : 0);
	w[1] = VG_WORD1(a->size, 0);	/* (the size: Apple packs stack arguments) */
	return pc;
}

static uint32_t vw_pcty(uint32_t pc) { return pc == VC_F32 ? VT_F32 : pc == VC_F64 ? VT_F64 : VT_I64; }

/* a slot word's value: the store table's, else a load */
static uint32_t vw_word(ecx *e, int64_t off, uint32_t ty)
{
	vwst *V = e->vw;
	int64_t rel;
	uint32_t slot = vw_slotat(e, off, 8, &rel);

	for (uint32_t i = V->nst; i-- > 0;)
		if (V->st[i].slot == slot && V->st[i].off == off) {
			uint32_t x = V->st[i].v;

			if (V->st[i].size == 8 || (V->st[i].size == 4 && ty == VT_F32))
				return vw_conv(e, x, ty == VT_I64 && vw_ty(e, x) == VT_PTR ? VT_PTR : ty);
		}
	V->sl[slot].loads++;
	{
		uint32_t v = vw_ins(e, VO_LOAD, ty, vw_mfl(ty == VT_F32 ? 4 : 8, 8, false), vw_slotaddr(e, slot, rel), 0, 0);

		V->p[v].slot = slot + 1;
		return v;
	}
}

static uint32_t vw_sig(ecx *e, const wkarg *a, int n, bool variadic, qty rt, uint32_t *rpc)
{
	vwst *V = e->vw;
	cc *c = e->c;
	uint32_t o = V->nx;

	GROW(V->x, V->nx, V->cx, 3 + 2 * (uint32_t)n);
	V->x[o] = VSIG_HDR((uint32_t)n, variadic);
	if (KIND(c, rt) == TY_VOID) {
		V->x[o + 1] = VG_WORD0(VG_NONE, 0, 0, 0, 0, 0, 0), V->x[o + 2] = 0;
		*rpc = 0;
	} else {
		wkarg r;

		vw_classify(e, rt, &r, false);
		*rpc = vw_group(&r, &V->x[o + 1]);
	}
	for (int k = 0; k < n; k++)
		vw_group(&a[k], &V->x[o + 3 + 2 * k]);
	V->nx += 3 + 2 * (uint32_t)n;
	return o;
}

VPRIV val vw_call(ecx *e, uint32_t ref, val *fn, const wkarg *a, int n, bool variadic, const wkarg *r, qty rt)
{
	vwst *V = e->vw;
	cc *c = e->c;
	uint32_t callee, sig, rpc, o, v;
	uint32_t *w;

	if (r)
		vw_fail(e, "VIR: a record result");
	for (int k = 0; k < n; k++)
		if (a[k].mem || a[k].n != 1)
			vw_fail(e, "VIR: a record argument");
	if (fn) {
		callee = vw_val(e, fn, VT_PTR);
		if (fn->k == V_REG)
			vw_tfree(e, fn->r);
	} else {
		callee = vw_k(e, VO_SYMADDR, VT_PTR, ref, 0, 0);
	}
	sig = vw_sig(e, a, n, variadic, rt, &rpc);
	o = V->nx;
	GROW(V->x, V->nx, V->cx, 1 + (uint32_t)n);
	V->nx += 1 + (uint32_t)n;
	V->x[o] = (uint32_t)n;
	for (int k = 0; k < n; k++) {
		uint32_t pc = a[k].cls[0] == WA_SSE ? (a[k].size == 4 ? VC_F32 : VC_F64) : VC_INT;
		uint32_t x = vw_word(e, a[k].slot, vw_pcty(pc));

		w = &V->x[o + 1 + k];
		*w = x;
	}
	v = vw_ins(e, VO_CALL, rpc ? vw_pcty(rpc) : VT_VOID, 0, callee, sig, o);
	vw_st_clear(e, true);
	if (!rpc)
		return vconst(0);
	if (rpc != VC_INT) {
		int q = vw_ftreg(e);

		vw_set(e, q, v);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
	{
		int q = vw_treg(e), size = wk_size(c, rt);
		bool sgn = wk_signed(c, rt);

		if (is_ptr(c, rt))
			v = vw_conv(e, v, VT_PTR);
		vw_set(e, q, v);
		if (size == 4 && !sgn)
			vw_set(e, q, vw_conv(e, v, VT_I32));
		else if (size < 8)
			vw_ext(e, q, size, sgn);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
}

VPRIV void vw_params(ecx *e, wkarg *a, int n, const wkarg *r, bool variadic)
{
	vwst *V = e->vw;
	cc *c = e->c;
	const node *body = N(c->syms[e->wk->fsym].body);

	if (r)
		vw_fail(e, "VIR: a record result");
	if (variadic && !DARWIN(c))
		vw_fail(e, "VIR: a variadic function (AAPCS64's register save areas)");
	uint32_t pv0 = V->np;	/* the params first (SPEC 15.2: the allocator reads them from the ABI registers) */

	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		qty ty = c->syms[sym].ty;
		uint32_t pc = a[k].cls[0] == WA_SSE ? (a[k].size == 4 ? VC_F32 : VC_F64) : VC_INT;

		if (a[k].mem || a[k].n != 1)
			vw_fail(e, "VIR: a record parameter");
		if (vw_ins(e, VO_PARAM, pc == VC_INT && is_ptr(c, ty) ? VT_PTR : vw_pcty(pc), 0, (uint32_t)k, 0, 0) != pv0 + (uint32_t)k)
			vw_fail(e, "VIR: parameters");
	}
	for (int k = 0; k < n; k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		qty ty = c->syms[sym].ty;
		uint32_t pc = a[k].cls[0] == WA_SSE ? (a[k].size == 4 ? VC_F32 : VC_F64) : VC_INT;
		uint32_t v = pv0 + (uint32_t)k;
		lval home;
		int q;

		q = pc == VC_INT ? vw_treg(e) : vw_ftreg(e);
		vw_set(e, q, v);
		if (pc == VC_INT && wk_size(c, ty) == 4)
			vw_set(e, q, vw_conv(e, v, VT_I32));	/* (the low half is the int; an i32 for the walker) */
		else if (pc == VC_INT && wk_size(c, ty) < 4)
			vw_ext(e, q, wk_size(c, ty), wk_signed(c, ty));
		if (vw_local(e, sym, &home)) {
			vw_store(e, q, &home, pc == VC_INT ? wk_size(c, ty) : (int)a[k].size);
		} else {
			lval s = { .k = L_FRAME, .off = a[k].slot, .align = 8 };

			vw_store(e, q, &s, 8);
		}
		vw_tfree(e, q);
	}
	V->nparam = (uint32_t)n;
	{
		uint32_t rpc;

		V->fsig = vw_sig(e, a, n, variadic, e->wk->rty, &rpc);
	}
}

VPRIV void vw_ret(ecx *e, val *v, qty t, uint32_t lbl)
{
	vwst *V = e->vw;
	cc *c = e->c;

	if (v) {
		uint32_t x;

		if (is_flt(c, t)) {
			x = vw_val(e, v, vw_fty(e, t));
		} else {
			int size = wk_size(c, t);
			bool sgn = wk_signed(c, t);

			if (v->k == V_CONST) {
				int64_t k = (int64_t)v->i;

				if (size < 8)
					k = sgn ? (int64_t)((uint64_t)k << (64 - 8 * size)) >> (64 - 8 * size) :
						  (int64_t)((uint64_t)k & ((1ull << (8 * size)) - 1));
				x = vw_iconst(e, is_ptr(c, t) ? VT_PTR : VT_I64, k);
			} else {
				int q = vw_toreg(e, v, size);

				if (size < 4)	/* (an int's upper half is unspecified in AAPCS64: a free zero extension) */
					vw_ext(e, q, size, sgn);
				else if (size == 4)
					vw_set(e, q, vw_get(e, q, VT_I32));
				x = vw_get(e, q, is_ptr(c, t) ? VT_PTR : VT_I64);
				vw_tfree(e, q);
			}
		}
		vw_write(e, VW_RETV, V->cur, x);
		V->rty = vw_ty(e, x);
	}
	vw_br(e, vw_lblk(e, lbl));
}

/* ---------------- the function ---------------- */

VPRIV void vw_begin(ecx *e, uint32_t s)
{
	vwst *V = vw_st(e);

	(void)s;
	V->np = 1;	/* (0 is no instruction) */
	GROW(V->p, 0, V->cp, 1);
	V->p[0] = (vwp){ 0 };
	V->nb = V->nx = V->nsl = V->nact = V->nst = V->nloc = V->nprom = 0;
	V->live[0] = V->live[1] = V->live[2] = 0;
	V->flive = 0;
	memset(V->fprod, 0, sizeof V->fprod);
	V->hn = 0;
	if (V->hcap)
		memset(V->h, 0, V->hcap * sizeof *V->h);
	if (V->clab)
		memset(V->lab, 0xff, V->clab * sizeof *V->lab);
	V->sealing = false;
	V->why = NULL;	/* (one function's reason is not the next one's) */
	V->rty = VT_VOID;
	V->nparam = 0;
	V->cur = vw_newblk(e);
	e->ndl = 0;
}

VPRIV void vw_finish(ecx *e, uint32_t s)
{
	vwst *V = e->vw;

	(void)s;
	{	/* the return block: ret, with the result if there is one */
		uint32_t w0 = V->x[V->fsig + 1], rty = VG_KIND(w0) == VG_REG ? vw_pcty(VG_CLASS(w0, 0)) : VT_VOID;
		uint32_t o, rv;

		if (rty == VT_I64 && V->rty == VT_PTR)
			rty = VT_PTR;
		rv = rty != VT_VOID ? vw_conv(e, vw_readv(e, VW_RETV, V->cur, rty), rty) : 0;
		o = V->nx;

		GROW(V->x, V->nx, V->cx, 2);
		V->x[V->nx++] = rv ? 1 : 0;
		if (rv)
			V->x[V->nx++] = rv;
		V->b[V->cur].term = vw_put(e, VO_RET, VT_VOID, 0, 0, 0, o, V->cur);
	}
	if (!vw_emit(e))
		vw_fail(e, V->why ? V->why : "VIR: the function could not be built");
}

VPRIV void vw_ecx_free(ecx *e)
{
	vwst *V = e->vw;

	if (!V)
		return;
	free(V->p), free(V->b), free(V->x), free(V->h), free(V->fwd), free(V->lab), free(V->sl), free(V->act);
	free(V->st), free(V->loc), free(V->vty), free(V->vsgn), free(V->pr), free(V->map), free(V->order), free(V->bmap);
	free(V);
	e->vw = NULL;
}

/* ---------------- sealing and emission ---------------- */


/* the successors of pre-block b, in operand order, through its terminator (n words at *out) */
static uint32_t vw_nsucc(vwst *V, uint32_t b)
{
	uint32_t t = V->b[b].term;

	if (!t)
		return 0;
	switch (V->p[t].in.op) {
	case VO_BR: return 1;
	case VO_BRIF: return 2;
	case VO_SWITCH: return 1 + V->x[V->p[t].in.c];
	default: return 0;
	}
}

static uint32_t *vw_succp(vwst *V, uint32_t b, uint32_t k)
{
	vir_ins *in = &V->p[V->b[b].term].in;

	switch (in->op) {
	case VO_BR: return &in->a;
	case VO_BRIF: return k ? &in->c : &in->b;
	default: {
		uint32_t st = V->p[in->a].in.ty == VT_I64 ? 5 : 3;

		return k ? &V->x[in->c + 1 + (k - 1) * st + st - 1] : &in->b;
	}
	}
}

/* a block with nothing in it that only jumps on: its predecessors jump on directly */
static void vw_thread(ecx *e)
{
	vwst *V = e->vw;

	for (uint32_t b = 0; b < V->nb; b++) {
		uint32_t n = vw_nsucc(V, b);

		for (uint32_t k = 0; k < n; k++) {
			uint32_t *t = vw_succp(V, b, k);

			for (int hop = 0; hop < 64; hop++) {
				vwb *q = &V->b[*t];

				if (*t == 0 || q->head || q->phead || q->def || !q->term || V->p[q->term].in.op != VO_BR || V->p[q->term].in.a == *t)
					break;
				*t = V->p[q->term].in.a;
			}
		}
	}
}

/* reachable blocks in reverse postorder (V->order) */
static void vw_rpo(ecx *e)
{
	vwst *V = e->vw;
	uint32_t n = 0, sp = 0;
	uint32_t *stk, *it;

	GROW(V->bmap, 0, V->cbmap, 2 * V->nb + 2);
	stk = V->bmap, it = V->bmap + V->nb + 1;
	GROW(V->order, 0, V->corder, V->nb + 1);
	for (uint32_t b = 0; b < V->nb; b++)
		V->b[b].reach = 0;
	V->b[0].reach = 1;
	stk[sp] = 0, it[sp++] = 0;
	while (sp) {
		uint32_t b = stk[sp - 1], k = it[sp - 1];

		if (k < vw_nsucc(V, b)) {
			uint32_t t = *vw_succp(V, b, k);

			it[sp - 1]++;
			if (!V->b[t].reach) {
				V->b[t].reach = 1;
				stk[sp] = t, it[sp++] = 0;
			}
			continue;
		}
		V->order[n++] = b;	/* postorder */
		sp--;
	}
	for (uint32_t i = 0; i < n / 2; i++) {
		uint32_t t = V->order[i];

		V->order[i] = V->order[n - 1 - i], V->order[n - 1 - i] = t;
	}
	V->norder = n;
}

/* the distinct reachable predecessors of every block */
static void vw_preds(ecx *e)
{
	vwst *V = e->vw;

	for (uint32_t b = 0; b < V->nb; b++)
		V->b[b].npred = 0;
	V->npr = 0;
	/* count (with repeats), then fill and drop repeats */
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i], n = vw_nsucc(V, b);

		for (uint32_t k = 0; k < n; k++)
			V->b[*vw_succp(V, b, k)].npred++;
	}
	for (uint32_t b = 0; b < V->nb; b++) {
		V->b[b].pred = V->npr;
		V->npr += V->b[b].npred;
		V->b[b].npred = 0;
	}
	GROW(V->pr, 0, V->cpr, V->npr + 1);
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i], n = vw_nsucc(V, b);

		for (uint32_t k = 0; k < n; k++) {
			vwb *t = &V->b[*vw_succp(V, b, k)];
			bool dup = false;

			for (uint32_t j = 0; j < t->npred; j++)
				dup |= V->pr[t->pred + j] == b;
			if (!dup)
				V->pr[t->pred + t->npred++] = b;
		}
	}
}

/* a parameter all of whose operands are one value (or itself) is that value */
static bool vw_trivial(ecx *e)
{
	vwst *V = e->vw;
	bool any = false;

	for (uint32_t i = 0; i < V->norder; i++) {
		vwb *k = &V->b[V->order[i]];

		for (uint32_t p = k->phead; p; p = V->p[p].next) {
			uint32_t same = 0;
			bool triv = true;

			if (V->fwd[p] != p)
				continue;
			for (uint32_t j = 0; j < k->npred; j++) {
				uint32_t o = vw_find(V, V->x[V->p[p].in.b + j]);

				if (o == p || o == same)
					continue;
				if (same) {
					triv = false;
					break;
				}
				same = o;
			}
			if (!triv)
				continue;
			V->fwd[p] = same ? same : vw_undef(e, V->p[p].in.ty);
			any = true;
		}
	}
	return any;
}

/* parameters nothing reads but other parameters (Braun's phi cycles, a local of a loop
 * that every path writes before reading) go: they would hold a register through the loop */
static void vw_mark(vwst *V, uint32_t *stk, uint32_t *n, uint32_t v)
{
	v = vw_find(V, v);
	if (V->p[v].in.op == VO_BPARAM && V->p[v].blk != VW_NOBLK && !V->map[v]) {
		V->map[v] = 1;
		stk[(*n)++] = v;
	}
}

static void vw_deadparams(ecx *e)
{
	vwst *V = e->vw;
	uint32_t *stk, n = 0;

	GROW(V->map, 0, V->cmap, V->np + 1);
	memset(V->map, 0, (size_t)V->np * sizeof *V->map);
	stk = malloc((size_t)(V->np + 1) * sizeof *stk);
	if (!stk)
		vw_fail(e, "out of memory");
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t q = V->b[b].head;; q = V->p[q].next) {
			uint32_t x = q ? q : V->b[b].term;
			const vir_ins *in;
			const vir_opinfo *oi;

			if (!x)
				break;
			in = &V->p[x].in;
			oi = &vir_optab[in->op];
			if (vw_find(V, x) == x && in->op != VO_BR) {
				for (int k = 0; k < 3; k++)
					if (vw_isval(oi->k[k]) && !(in->op == VO_BRIF && k > 0))
						vw_mark(V, stk, &n, (&in->a)[k]);
				if (in->op == VO_CALL || in->op == VO_RET)
					for (uint32_t k = 0; k < V->x[in->c]; k++)
						vw_mark(V, stk, &n, V->x[in->c + 1 + k]);
			}
			if (!q)
				break;
		}
	}
	while (n) {	/* a live parameter's operands are live */
		uint32_t p = stk[--n], b = V->p[p].blk;

		for (uint32_t j = 0; j < V->b[b].npred; j++)
			vw_mark(V, stk, &n, V->x[V->p[p].in.b + j]);
	}
	for (uint32_t i = 0; i < V->norder; i++) {
		vwb *k = &V->b[V->order[i]];

		for (uint32_t p = k->phead; p; p = V->p[p].next)
			if (V->fwd[p] == p && !V->map[p])
				V->fwd[p] = vw_undef(e, V->p[p].in.ty);
	}
	free(stk);
}

/* ---------------- slots to variables ---------------- */

#define VW_NOCONV UINT32_MAX

typedef struct vwfld { uint32_t slot; int64_t rel; uint32_t size, ty; } vwfld;

/* the conversion a load of type ty (flags fl) applies to the stored value of type st: VW_NOCONV
 * none, VO_EXT/P2I/BITCAST one instruction; false if it takes more */
static bool vw_ldconv(uint32_t st, uint32_t ty, uint32_t fl, uint32_t *op)
{
	uint32_t w = VIF_W(fl);

	if (w < 2 || (fl & VIF_MS)) {	/* a narrow or sign-extending load: the low bytes, extended */
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

static bool vw_kint(const vwst *V, uint32_t v, int64_t *k);

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
static bool vw_promdead(const vwst *V, uint32_t q)
{
	const vwp *x = &V->p[q];

	return (x->in.op == VO_STORE || x->in.op == VO_MEMSET) && x->slot && V->sl[x->slot - 1].prom;
}

/* a slot whose address does not escape, read and written only as fields (offset, size) that
 * do not overlap, becomes one variable per field: its loads read the value the field holds
 * (Braun, as the walker's locals), its stores go */
static bool vw_promote(ecx *e)
{
	vwst *V = e->vw;
	vwfld *fd = NULL;
	uint32_t nf = 0, cf = 0, *fof, *cur, *cst, np0 = V->np, var0;
	uint8_t *bad;
	bool done = false;
	int64_t rel;
	struct vwms *ms = NULL;	/* the memsets of slots with 0 */
	uint32_t nms = 0, cms = 0;

	if (!V->nsl)
		return false;
	bad = calloc(V->nsl, 1);
	fof = malloc((size_t)V->np * sizeof *fof);	/* access -> field + 1 */
	if (!bad || !fof)
		vw_fail(e, "out of memory");
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
		for (k = 0; k < nf; k++) {
			vwfld *f = &fd[k];

			if (f->slot != s)
				continue;
			if (f->rel == rel && f->size == size)
				break;
			if (f->rel < rel + (int64_t)size && rel < f->rel + (int64_t)f->size)
				bad[s] = 1;	/* overlapping fields */
		}
		if (bad[s])
			continue;
		if (k == nf) {
			GROW(fd, nf, cf, 1);
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
		for (uint32_t k = 0; k < nf; k++) {
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
	/* a field of pointer and integer stores holds the integer: a pointer store's value goes
	 * through a p2i right after the store (which promotion makes dead) */
	for (uint32_t i = 0; i < np0; i++) {
		uint32_t p, b;

		if (V->p[i].in.op != VO_STORE || !fof[i] || bad[V->p[i].slot - 1] || vw_find(V, i) != i ||
		    fd[fof[i] - 1].ty != VT_I64 || V->p[V->p[i].in.b].in.ty != VT_PTR)
			continue;
		b = V->p[i].blk;
		p = vw_put(e, VO_P2I, VT_I64, 0, V->p[i].in.b, 0, 0, b);
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
		vw_fail(e, "out of memory");
	cst = cur + nf;
	/* each block's last store to a field is the field's value at its end */
	for (uint32_t oi = 0; oi < V->norder; oi++) {
		uint32_t b = V->order[oi];

		for (uint32_t q = V->b[b].head; q; q = V->p[q].next) {
			if (q >= np0 || !fof[q] || bad[V->p[q].slot - 1] || V->p[q].in.op == VO_LOAD || vw_find(V, q) != q)
				continue;
			for (uint32_t k = 0; k < nf; k++) {
				uint32_t var = VW_VAR0 + var0 + k;

				if (V->p[q].in.op == VO_STORE ? k != fof[q] - 1 : !vw_mscov(&ms[UINT32_MAX - fof[q]], &fd[k]))
					continue;
				vw_write(e, var, b, V->p[q].in.op == VO_STORE ? V->p[q].in.b : vw_zerok(e, fd[k].ty));
				*vw_hslot(e, b, var | 0x40000000u, true) = 1;	/* b defines var */
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

				for (k = 0; k < nf; k++)
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
	free(fof);
	free(bad);
	return done;
}

/* ---------------- simplification ----------------
 *
 * The walker's output, instruction by instruction, carries what its direct target folds as it
 * emits: extensions of values already extended, a truncation of a widening, x * 1, a chain of
 * constant offsets, the same address computed again, the same byte loaded again.  One walk
 * over the dominator tree rewrites each instruction (vw_simp1) and then looks it up: a pure
 * one among the block's earlier ones, a load among those of the block and of the blocks that
 * lead to it alone (a block with one predecessor continues its memory state; anything that
 * may write memory starts a new one, and a store remembers its value for a load of the same
 * address and type).  Constants are numbered first: the walker makes one per use.  What
 * nothing uses any more goes at the end. */

typedef struct vwhe { uint32_t op, ty, fl, a, b, c, gen, v; } vwhe;	/* (key) -> value */

typedef struct vwsimp {
	uint32_t *cnt, n0;	/* uses per value; values from n0 on are the pass's constants */
	vwhe *ht; uint32_t hmask;	/* pure instructions and loads of the dominators, by content */
	uint32_t *log, nlog;	/* ... the entries added, in order (undone leaving a block) */
	uint32_t *kt, kmask, ncst, nk;	/* constants: value + 1 */
	uint32_t memgen, gens, *egen;	/* the memory state loads are keyed by; per block at its end */
	uint32_t blk;		/* the block pure instructions are keyed by: one kept live into other
				 * blocks costs a register more often than it saves an instruction */
	uint32_t *idom, *dkid, *dnext;	/* the dominator tree */
	uint32_t undef[VT_N];
} vwsimp;

static void vw_inc(vwst *V, vwsimp *S, uint32_t v)
{
	if (v < S->n0 && V->p[v].blk != VW_NOBLK)
		S->cnt[v]++;
}

static void vw_dec(vwst *V, vwsimp *S, uint32_t v)
{
	if (v < S->n0 && V->p[v].blk != VW_NOBLK && S->cnt[v])
		S->cnt[v]--;
}

/* the value operands of x (a parameter's: one per predecessor), as words that can be rewritten */
#define VW_EACHOP(V_, X_, O_, BODY_) do { \
	vwp *x_ = &(V_)->p[X_]; \
	const vir_opinfo *oi_ = &vir_optab[x_->in.op]; \
	if (x_->in.op == VO_BPARAM) { \
		if (x_->in.b != VW_NOBLK) \
			for (uint32_t j_ = 0; j_ < (V_)->b[x_->blk].npred; j_++) { \
				uint32_t *O_ = &(V_)->x[x_->in.b + j_]; BODY_; \
			} \
		break; \
	} \
	for (int k_ = 0; k_ < 3; k_++) \
		if (vw_isval(oi_->k[k_]) && !(x_->in.op == VO_BRIF && k_ > 0)) { \
			uint32_t *O_ = &(&x_->in.a)[k_]; BODY_; \
		} \
	if (x_->in.op == VO_CALL || x_->in.op == VO_RET) \
		for (uint32_t k_ = 0; k_ < (V_)->x[x_->in.c]; k_++) { \
			uint32_t *O_ = &(V_)->x[x_->in.c + 1 + k_]; BODY_; \
		} \
} while (0)

static bool vw_kint(const vwst *V, uint32_t v, int64_t *k)
{
	const vir_ins *in = &V->p[v].in;

	if (in->op != VO_ICONST)
		return false;
	*k = in->ty == VT_I32 ? (int64_t)(int32_t)in->b : (int64_t)((uint64_t)in->b | (uint64_t)in->c << 32);
	return true;
}

/* bytes v is zero-extended from (its type's size: nothing known) */
static uint32_t vw_zw(const vwst *V, uint32_t v)
{
	const vir_ins *in = &V->p[v].in;
	uint32_t sz = vir_tysize(in->ty);
	int64_t k;

	switch (in->op) {
	case VO_LOAD:
		return (in->fl & VIF_MS) ? sz : 1u << VIF_W(in->fl) < sz ? 1u << VIF_W(in->fl) : sz;
	case VO_EXT:
		return (in->fl & VIF_MS) ? sz : 1u << VIF_W(in->fl);
	case VO_ICMP: case VO_FCMP:
		return 1;
	case VO_AND:
		if (vw_kint(V, in->b, &k) && k >= 0)
			return k <= 0xff ? 1 : k <= 0xffff ? 2 : k <= 0xffffffffll ? 4 : sz;
		return sz;
	default:
		return sz;
	}
}

/* bytes v is sign-extended from */
static uint32_t vw_sw(const vwst *V, uint32_t v)
{
	const vir_ins *in = &V->p[v].in;
	uint32_t sz = vir_tysize(in->ty), z;

	if ((in->op == VO_LOAD || in->op == VO_EXT) && (in->fl & VIF_MS))
		return 1u << VIF_W(in->fl) < sz ? 1u << VIF_W(in->fl) : sz;
	z = vw_zw(V, v);
	return z < sz ? 2 * z : sz;	/* (zero-extended from z bytes: a signed 2z-byte value) */
}

static uint32_t vw_undefk(ecx *e, vwsimp *S, uint32_t ty)
{
	if (!S->undef[ty])
		S->undef[ty] = vw_undef(e, ty);
	return S->undef[ty];
}

static bool vw_commutes(uint32_t op);

/* q rewritten in place (true), or the value that replaces it in *r */
static bool vw_simp1(ecx *e, vwsimp *S, uint32_t q, uint32_t *r)
{
	vwst *V = e->vw;
	vir_ins in = V->p[q].in;
	int64_t k, k2;

	*r = q;
	if (vw_commutes(in.op) && vw_kint(V, in.a, &k) && !vw_kint(V, in.b, &k2)) {
		V->p[q].in.a = in.b, V->p[q].in.b = in.a;	/* the constant second (an immediate) */
		in = V->p[q].in;
	}
	switch (in.op) {
	case VO_I2P: case VO_P2I:	/* a pointer and its address as an integer are the same bits */
		if (V->p[in.a].in.op == (in.op == VO_I2P ? VO_P2I : VO_I2P))
			*r = V->p[in.a].in.a;
		return false;
	case VO_EXT: {
		uint32_t x = in.a, K = 1u << VIF_W(in.fl), ts = vir_tysize(in.ty);
		bool s = in.fl & VIF_MS;
		vir_ins xi = V->p[x].in;
		uint32_t xs = vir_tysize(xi.ty);

		if (vw_kint(V, x, &k)) {
			uint64_t u = (uint64_t)k & (K == 8 ? ~0ull : (1ull << 8 * K) - 1);

			if (s && K < 8 && (u >> (8 * K - 1)) & 1)
				u |= ~0ull << 8 * K;
			*r = vw_iconst(e, in.ty, (int64_t)u);
			return false;
		}
		if (K >= ts) {	/* (the whole value: a copy or a plain widening) */
			if (xs == ts) {
				*r = x;
				return false;
			}
		}
		if (xs == ts && (s ? vw_sw(V, x) <= K : vw_zw(V, x) <= K)) {
			*r = x;
			return false;
		}
		if (xs == ts && s && vw_zw(V, x) < K) {
			*r = x;
			return false;
		}
		if (xi.op == VO_EXT && vir_tysize(V->p[xi.a].in.ty) <= ts) {
			uint32_t M = 1u << VIF_W(xi.fl);

			if (K <= M) {	/* the low K bytes of x are y's */
				vw_dec(V, S, x), vw_inc(V, S, xi.a);
				V->p[q].in.a = xi.a;
				return true;
			}
			if (s == !!(xi.fl & VIF_MS) || !(xi.fl & VIF_MS)) {	/* x's extension is q's */
				vw_dec(V, S, x), vw_inc(V, S, xi.a);
				V->p[q].in.a = xi.a;
				V->p[q].in.fl = xi.fl;
				return true;
			}
		}
		/* a load only q reads: the load extends */
		if (xi.op == VO_LOAD && !(xi.fl & VIF_VOL) && S->cnt[x] == 1 && (1u << VIF_W(xi.fl)) == K) {
			V->p[x].in.ty = (uint8_t)in.ty;
			V->p[x].in.fl = (uint16_t)((xi.fl & ~VIF_MS) | (s ? VIF_MS : 0));
			*r = x;
			return false;
		}
		if (xi.op == VO_LOAD && !(xi.fl & VIF_VOL) && S->cnt[x] == 1 && (1u << VIF_W(xi.fl)) < K &&
		    (s ? vw_sw(V, x) <= K : vw_zw(V, x) <= K)) {
			V->p[x].in.ty = (uint8_t)in.ty;
			*r = x;
			return false;
		}
		return false;
	}
	case VO_TRUNC: {
		uint32_t x = in.a;
		vir_ins xi = V->p[x].in;

		if (vw_kint(V, x, &k)) {
			*r = vw_iconst(e, VT_I32, (int64_t)(uint32_t)k);
			return false;
		}
		if (xi.op == VO_EXT && V->p[xi.a].in.ty == VT_I32 && VIF_W(xi.fl) >= 2) {
			*r = xi.a;
			return false;
		}
		if (xi.op == VO_EXT && V->p[xi.a].in.ty == VT_I32) {	/* a narrower extension, in i32 */
			vw_dec(V, S, x), vw_inc(V, S, xi.a);
			V->p[q].in = (vir_ins){ VO_EXT, VT_I32, xi.fl, xi.a, 0, 0 };
			return true;
		}
		if (xi.op == VO_EXT && VIF_W(xi.fl) >= 2) {	/* trunc of a widening of the low half */
			vw_dec(V, S, x), vw_inc(V, S, xi.a);
			V->p[q].in.a = xi.a;
			return true;
		}
		if (xi.op == VO_LOAD && !(xi.fl & VIF_VOL) && S->cnt[x] == 1 && VIF_W(xi.fl) <= 2) {
			V->p[x].in.ty = VT_I32;
			if (VIF_W(xi.fl) == 2)
				V->p[x].in.fl &= (uint16_t)~VIF_MS;
			*r = x;
			return false;
		}
		return false;
	}
	case VO_ADD: case VO_SUB: case VO_OR: case VO_XOR: case VO_SHL: case VO_LSHR: case VO_ASHR:
		if (vw_kint(V, in.b, &k) && !k)
			*r = in.a;
		return false;
	case VO_MUL:
		if (vw_kint(V, in.b, &k) && k == 1)
			*r = in.a;
		return false;
	case VO_AND:
		if (vw_kint(V, in.b, &k) && (k == -1 || (in.ty == VT_I32 && (uint32_t)k == 0xffffffffu)))
			*r = in.a;
		else if (vw_kint(V, in.b, &k) && k > 0 && !(k & (k + 1)) && vw_zw(V, in.a) < 8 &&
			 (uint64_t)k >= (vw_zw(V, in.a) == 8 ? ~0ull : (1ull << 8 * vw_zw(V, in.a)) - 1))
			*r = in.a;	/* the mask keeps every bit that can be set */
		return false;
	case VO_PTRADD:
		if (!vw_kint(V, in.b, &k))
			return false;
		if (!k) {
			*r = in.a;
			return false;
		}
		if (V->p[in.a].in.op == VO_SLOTADDR && V->p[in.a].blk == VW_NOBLK) {	/* $s+k2 + k: $s+(k2+k) */
			vir_ins a = V->p[in.a].in;	/* (a copy: vw_k can move V->p) */

			k2 = (int64_t)((uint64_t)a.b | (uint64_t)a.c << 32) + k;
			*r = vw_k(e, VO_SLOTADDR, VT_PTR, a.a, (uint32_t)k2, (uint32_t)((uint64_t)k2 >> 32));
			return false;
		}
		if (V->p[in.a].in.op == VO_PTRADD && V->p[in.a].blk != VW_NOBLK && vw_kint(V, V->p[in.a].in.b, &k2)) {
			uint32_t y = V->p[in.a].in.a, c = vw_iconst(e, VT_I64, (int64_t)((uint64_t)k + (uint64_t)k2));

			vw_dec(V, S, in.a), vw_inc(V, S, y);
			V->p[q].in.a = y, V->p[q].in.b = c;
			return true;
		}
		return false;
	default:
		return false;
	}
}

static bool vw_commutes(uint32_t op)
{
	return op == VO_ADD || op == VO_MUL || op == VO_AND || op == VO_OR || op == VO_XOR ||
	       op == VO_UMULHI || op == VO_SMULHI;
}

/* the table's entry for key x (an empty one when it has none) */
static vwhe *vw_hlook(vwsimp *S, const vwhe *x)
{
	uint64_t h = (uint64_t)x->op * 0x9e3779b97f4a7c15ull;

	h = (h ^ x->ty ^ (uint64_t)x->fl << 8) * 0xff51afd7ed558ccdull;
	h = (h ^ x->a ^ (uint64_t)x->b << 32) * 0xc4ceb9fe1a85ec53ull;
	h = (h ^ x->c ^ (uint64_t)x->gen << 32) * 0x9e3779b97f4a7c15ull;
	for (uint32_t i = (uint32_t)(h >> 40) & S->hmask;; i = (i + 1) & S->hmask) {
		vwhe *t = &S->ht[i];

		if (!t->v || (t->op == x->op && t->ty == x->ty && t->fl == x->fl && t->a == x->a &&
			      t->b == x->b && t->c == x->c && t->gen == x->gen))
			return t;
	}
}

/* one pre-IR value per constant (the walker makes one per use) */
static uint32_t vw_kcanon(vwst *V, vwsimp *S, uint32_t v)
{
	const vir_ins *in = &V->p[v].in;
	uint64_t h;

	if (V->p[v].blk != VW_NOBLK || in->op == VO_BPARAM)
		return v;
	h = ((uint64_t)in->op << 8 | in->ty) * 0x9e3779b97f4a7c15ull;
	h = (h ^ in->a ^ (uint64_t)in->b << 32) * 0xff51afd7ed558ccdull;
	h = (h ^ in->c) * 0xc4ceb9fe1a85ec53ull;
	for (uint32_t i = (uint32_t)(h >> 40) & S->kmask;; i = (i + 1) & S->kmask) {
		uint32_t k = S->kt[i];

		if (!k) {
			if (2 * ++S->nk > S->kmask)
				return v;	/* (full: v as it is) */
			S->kt[i] = v + 1;
			return v;
		}
		k--;
		if (V->p[k].in.op == in->op && V->p[k].in.ty == in->ty && V->p[k].in.a == in->a &&
		    V->p[k].in.b == in->b && V->p[k].in.c == in->c)
			return k;
	}
}

/* the block's instructions, rewritten and numbered (S->ht holds what its dominators computed) */
static void vw_cse_blk(ecx *e, vwsimp *S_, uint32_t b)
{
	vwst *V = e->vw;
	vwsimp *S = S_;

	for (uint32_t q = V->b[b].head; q; q = V->p[q].next) {
		uint32_t r, op, eff;
		vwhe key, *t;

		if (vw_find(V, q) != q)
			continue;
	again:
		VW_EACHOP(V, q, o, *o = vw_kcanon(V, S, vw_find(V, *o)));
		for (int it = 0; vw_simp1(e, S, q, &r) && it < 8; it++)
			;	/* (rewritten in place: again) */
		if (r != q) {
			r = vw_kcanon(V, S, vw_find(V, r));
			if (r < S->n0 && V->p[r].blk != VW_NOBLK)
				S->cnt[r] += S->cnt[q];
			VW_EACHOP(V, q, o, vw_dec(V, S, *o));
			S->cnt[q] = 0;
			V->fwd[q] = r;
			continue;
		}
		op = V->p[q].in.op;
		eff = vir_optab[op].eff;
		if (vw_promdead(V, q))
			continue;
		if (eff & (EF_W | EF_C | EF_A) || op == VO_BARRIER || op == VO_VA_ARG || op == VO_VA_START)
			S->memgen = ++S->gens;
		if (op == VO_STORE) {
			vir_ins in = V->p[q].in;
			uint32_t vt = V->p[in.b].in.ty;

			if (!(in.fl & VIF_VOL) && vir_tysize(vt) && 1u << VIF_W(in.fl) == vir_tysize(vt)) {
				key = (vwhe){ VO_LOAD, vt, VIF_W(in.fl), in.a, 0, 0, S->memgen, 0 };
				t = vw_hlook(S, &key);
				if (!t->v)
					S->log[S->nlog++] = (uint32_t)(t - S->ht);
				*t = key;
				t->v = in.b;
			}
			if (!(in.fl & VIF_VOL) && (vt == VT_I32 || vt == VT_I64)) {	/* (any load of its width) */
				key = (vwhe){ VO_STORE, 0, VIF_W(in.fl), in.a, 0, 0, S->memgen, 0 };
				t = vw_hlook(S, &key);
				if (!t->v)
					S->log[S->nlog++] = (uint32_t)(t - S->ht);
				*t = key;
				t->v = in.b;
			}
			continue;
		}
		if (op == VO_LOAD) {
			vir_ins in = V->p[q].in;

			if (in.fl & VIF_VOL)
				continue;
			key = (vwhe){ VO_LOAD, in.ty, in.fl & (3 | VIF_MS), in.a, 0, 0, S->memgen, 0 };
			if ((in.ty == VT_I32 || in.ty == VT_I64) && !vw_hlook(S, &key)->v) {
				/* the value a store of this width left: truncated or extended to the load's */
				vwhe sk = { VO_STORE, 0, VIF_W(in.fl), in.a, 0, 0, S->memgen, 0 };
				uint32_t v = vw_hlook(S, &sk)->v, w = VIF_W(in.fl), vt;

				if (v) {
					v = vw_find(V, v);
					vt = V->p[v].in.ty;
					if (in.ty == VT_I32 && vt == VT_I64 && w == 2)
						V->p[q].in = (vir_ins){ VO_TRUNC, VT_I32, 0, v, 0, 0 };
					else if (w < 3 && vir_tysize(vt) <= vir_tysize(in.ty))
						V->p[q].in = (vir_ins){ VO_EXT, (uint8_t)in.ty, (uint16_t)(w | (in.fl & VIF_MS)), v, 0, 0 };
					else
						v = 0;
					if (v) {
						vw_dec(V, S, in.a), vw_inc(V, S, v);
						goto again;
					}
				}
			}
		} else if ((eff & ~EF_UF) == EF_P && op != VO_BPARAM && op != VO_PARAM && op != VO_PROJ) {
			vir_ins in = V->p[q].in;

			if (vw_commutes(op) && in.a > in.b)
				key = (vwhe){ op, in.ty, in.fl, in.b, in.a, in.c, S->blk, 0 };
			else
				key = (vwhe){ op, in.ty, in.fl, in.a, in.b, in.c, S->blk, 0 };
		} else
			continue;
		t = vw_hlook(S, &key);
		if (t->v && op == VO_LOAD && V->p[vw_find(V, t->v)].in.ty != V->p[q].in.ty) {
			/* the same bytes, loaded as the other integer type (a truncation retyped it) */
			vir_ins in = V->p[q].in;
			uint32_t v = vw_find(V, t->v), vt = V->p[v].in.ty;

			if (in.ty == VT_I32 && vt == VT_I64)
				V->p[q].in = (vir_ins){ VO_TRUNC, VT_I32, 0, v, 0, 0 };
			else if (in.ty == VT_I64 && vt == VT_I32 && VIF_W(in.fl) < 3)
				V->p[q].in = (vir_ins){ VO_EXT, VT_I64, (uint16_t)(in.fl & (3 | VIF_MS)), v, 0, 0 };
			else
				v = 0;
			if (v) {
				vw_dec(V, S, in.a), vw_inc(V, S, v);
				goto again;
			}
		}
		if (t->v && V->p[vw_find(V, t->v)].in.ty == V->p[q].in.ty) {
			r = vw_find(V, t->v);
			if (r < S->n0 && V->p[r].blk != VW_NOBLK)
				S->cnt[r] += S->cnt[q];
			VW_EACHOP(V, q, o, vw_dec(V, S, *o));
			S->cnt[q] = 0;
			V->fwd[q] = r;
			continue;
		}
		if (!t->v) {
			S->log[S->nlog++] = (uint32_t)(t - S->ht);
			*t = key;
			t->v = q;
		}
	}
}

/* immediate dominators (Cooper, Harvey, Kennedy) and the tree's children, in reverse postorder */
static void vw_doms(ecx *e, vwsimp *S)
{
	vwst *V = e->vw;
	uint32_t n = V->norder, *ri = malloc(((size_t)V->nb + 1) * sizeof *ri);
	bool changed = true;

	S->idom = malloc(((size_t)V->nb + 1) * sizeof *S->idom);
	S->dkid = malloc(((size_t)V->nb + 1) * sizeof *S->dkid);
	S->dnext = malloc(((size_t)V->nb + 1) * sizeof *S->dnext);
	if (!ri || !S->idom || !S->dkid || !S->dnext)
		vw_fail(e, "out of memory");
	for (uint32_t b = 0; b < V->nb; b++)
		ri[b] = VW_NOBLK, S->idom[b] = VW_NOBLK, S->dkid[b] = VW_NOBLK, S->dnext[b] = VW_NOBLK;
	for (uint32_t i = 0; i < n; i++)
		ri[V->order[i]] = i;
	S->idom[V->order[0]] = V->order[0];
	while (changed) {
		changed = false;
		for (uint32_t i = 1; i < n; i++) {
			uint32_t b = V->order[i], d = VW_NOBLK;

			for (uint32_t j = 0; j < V->b[b].npred; j++) {
				uint32_t p = V->pr[V->b[b].pred + j];

				if (ri[p] == VW_NOBLK || S->idom[p] == VW_NOBLK)
					continue;
				if (d == VW_NOBLK) {
					d = p;
					continue;
				}
				while (p != d) {
					while (ri[p] > ri[d])
						p = S->idom[p];
					while (ri[d] > ri[p])
						d = S->idom[d];
				}
			}
			if (d != S->idom[b]) {
				S->idom[b] = d;
				changed = true;
			}
		}
	}
	for (uint32_t i = n; i-- > 1;) {	/* (pushed in reverse: kept in order) */
		uint32_t b = V->order[i], d = S->idom[b];

		S->dnext[b] = S->dkid[d];
		S->dkid[d] = b;
	}
	free(ri);
}

static void vw_simplify(ecx *e)
{
	vwst *V = e->vw;
	vwsimp S = { 0 };
	uint32_t *stk, ns = 0;

	S.n0 = V->np;
	S.cnt = calloc((size_t)S.n0 + 1, sizeof *S.cnt);
	stk = malloc(((size_t)S.n0 + 1) * sizeof *stk);
	{
		uint32_t kc = 16;

		for (uint32_t i = 0; i < V->np; i++)
			S.ncst += V->p[i].blk == VW_NOBLK;
		while (kc < 2 * (V->np + 64))	/* (and the constants the pass makes) */
			kc *= 2;
		S.kt = calloc(kc, sizeof *S.kt);
		S.kmask = kc - 1;
	}
	if (!S.cnt || !stk || !S.kt)
		vw_fail(e, "out of memory");
	/* uses */
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p)
				VW_EACHOP(V, p, o, vw_inc(V, &S, vw_find(V, *o)));
		for (uint32_t q = V->b[b].head;; q = V->p[q].next) {
			uint32_t x = q ? q : V->b[b].term;

			if (!x)
				break;
			if (vw_find(V, x) == x &&
			    !vw_promdead(V, x))
				VW_EACHOP(V, x, o, vw_inc(V, &S, vw_find(V, *o)));
			if (!q)
				break;
		}
	}
	/* rewriting and numbering over the dominator tree */
	vw_doms(e, &S);
	{
		uint32_t cap = 16, top = 0, *bst = malloc((size_t)(V->nb + 1) * 2 * sizeof *bst);

		while (cap < 2 * (V->np - S.ncst) + 2)
			cap *= 2;
		S.ht = calloc(cap, sizeof *S.ht);
		S.hmask = cap - 1;
		S.log = malloc(((size_t)V->np + 1) * sizeof *S.log);
		S.egen = malloc(((size_t)V->nb + 1) * sizeof *S.egen);
		if (!S.ht || !S.log || !bst || !S.egen)
			vw_fail(e, "out of memory");
		bst[top++] = V->order[0], bst[top++] = 0;
		while (top) {
			uint32_t ph = bst[--top], b = bst[--top];

			if (ph) {	/* leaving b: its entries go */
				while (S.nlog > ph - 1) {
					S.nlog--;
					S.ht[S.log[S.nlog]].v = 0;
				}
				continue;
			}
			bst[top++] = b, bst[top++] = S.nlog + 1;
			S.memgen = V->b[b].npred == 1 ? S.egen[V->pr[V->b[b].pred]] : ++S.gens;
			S.blk = ++S.gens;
			vw_cse_blk(e, &S, b);
			S.egen[b] = S.memgen;
			for (uint32_t c = S.dkid[b]; c != VW_NOBLK; c = S.dnext[c])
				bst[top++] = c, bst[top++] = 0;
		}
		free(bst);
	}
	/* what nothing reads */
	for (uint32_t i = 0; i < V->norder; i++)
		for (uint32_t q = V->b[V->order[i]].head; q; q = V->p[q].next)
			if (vw_find(V, q) == q && !S.cnt[q])
				stk[ns++] = q;
	while (ns) {
		uint32_t q = stk[--ns], op = V->p[q].in.op, eff = vir_optab[op].eff;

		if (vw_find(V, q) != q || S.cnt[q] || V->p[q].blk == VW_NOBLK)
			continue;
		if (op == VO_LOAD ? (V->p[q].in.fl & VIF_VOL) != 0 :
		    ((eff & ~(EF_UF | EF_F)) != EF_P || op == VO_BPARAM || op == VO_PARAM))	/* (FP status flags are not kept: no FENV_ACCESS) */
			continue;
		VW_EACHOP(V, q, o, {
			uint32_t x = vw_find(V, *o);

			vw_dec(V, &S, x);
			if (x < S.n0 && V->p[x].blk != VW_NOBLK && !S.cnt[x] && V->p[x].in.op != VO_BPARAM)
				stk[ns++] = x;
		});
		V->fwd[q] = vw_undefk(e, &S, V->p[q].in.ty);
	}
	free(S.ht), free(S.log), free(S.egen), free(S.kt);
	free(S.idom), free(S.dkid), free(S.dnext);
	free(S.cnt);
	free(stk);
}

/*
 * If-conversion, as LLVM's SimplifyCFG folds a two-entry phi: b ends in brif c, T, F
 * where the arms hold nothing (or at most two cheap instructions without effects
 * each, which move up into b) and meet in J (a diamond: both arms empty blocks of b's
 * alone; a triangle: one arm, the other edge goes to J itself) and J's parameters
 * differ between the two entries in one value only.  That one becomes select c, t, f
 * at the end of b (its operands are defined in b or above it now), b jumps to
 * J and the arms go.  One select: its compare is still read once and sets the flags
 * the csel reads.
 */
static bool vw_armempty(vwst *V, uint32_t a, uint32_t b, uint32_t cond, uint32_t *n)
{
	const vwb *k = &V->b[a];

	if (a == b || !k->term || V->p[k->term].in.op != VO_BR || k->npred != 1 || V->pr[k->pred] != b)
		return false;
	for (uint32_t p = k->phead; p; p = V->p[p].next)
		if (V->fwd[p] == p)
			return false;
	for (uint32_t q = k->head; q; q = V->p[q].next) {
		uint32_t op = V->p[q].in.op;

		if (vw_find(V, q) != q || vw_promdead(V, q) || (op == VO_MEMSET && (V->p[q].in.fl & VW_LIFETIME)))
			continue;
		/* cheap and without effects: computed whichever way the branch went (at most 2) */
		if ((op != VO_ADD && op != VO_SUB && op != VO_AND && op != VO_OR && op != VO_XOR && op != VO_SHL && op != VO_LSHR &&
		     op != VO_ASHR && op != VO_EXT && op != VO_TRUNC && op != VO_I2P && op != VO_P2I && op != VO_PTRADD) ||
		    V->p[q].in.a == cond || V->p[q].in.b == cond || ++*n > 2)
			return false;
	}
	return true;
}

/* arm a's instructions into b, before b's last instruction when that is the condition
 * (the select stays next to its compare) */
static void vw_hoist(vwst *V, uint32_t a, uint32_t b, uint32_t cond)
{
	vwb *k = &V->b[b];
	uint32_t prev = 0, last = 0, nx;

	if (k->tail == cond) {
		/* a joined condition (vw_condmerge: compare, compare, and/or ...) ends b: before all of it,
		 * when it is b's last instructions and the arm reads none of them */
		uint32_t m[16], nm = 0, v = cond;
		bool ok = true;

		last = cond;
		while (nm + 2 <= 16 && (V->p[v].in.op == VO_AND || V->p[v].in.op == VO_OR) && V->p[v].in.ty == VT_I32 &&
		       V->p[v].blk == b && V->p[vw_find(V, V->p[v].in.b)].in.op == VO_ICMP) {
			m[nm++] = v, m[nm++] = vw_find(V, V->p[v].in.b);
			v = vw_find(V, V->p[v].in.a);
		}
		if (nm && V->p[v].in.op == VO_ICMP && V->p[v].blk == b) {
			uint32_t q, left = nm + 1;

			m[nm++] = v;
			for (q = v; q && left; q = V->p[q].next, left--) {	/* v ... cond: exactly the members, in a row */
				bool in = false;

				for (uint32_t j = 0; j < nm; j++)
					in |= m[j] == q;
				if (!in)
					break;
			}
			ok = !q && !left;
			for (q = V->b[a].head; q && ok; q = V->p[q].next)
				for (uint32_t j = 0; j < nm; j++)
					if (vw_find(V, V->p[q].in.a) == m[j] || vw_find(V, V->p[q].in.b) == m[j])
						ok = false;
			if (ok)
				last = v;
		}
		for (uint32_t q = k->head; q != last; q = V->p[q].next)
			prev = q;
	} else {
		prev = k->tail;
	}
	for (uint32_t q = V->b[a].head; q; q = nx) {
		nx = V->p[q].next;
		if (vw_find(V, q) != q || vw_promdead(V, q) || V->p[q].in.op == VO_MEMSET)
			continue;
		V->p[q].blk = b;
		V->p[q].next = last;
		if (prev)
			V->p[prev].next = q;
		else
			k->head = q;
		prev = q;
	}
	if (!last)
		k->tail = prev;
	V->b[a].head = V->b[a].tail = 0;
}

static uint32_t vw_predix(const vwst *V, uint32_t j, uint32_t b)
{
	for (uint32_t i = 0; i < V->b[j].npred; i++)
		if (V->pr[V->b[j].pred + i] == b)
			return i;
	return VW_NOBLK;
}

static bool vw_ifconv(ecx *e)
{
	vwst *V = e->vw;
	bool any = false;

	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i], t = V->b[b].term, T, F, J, it, jf, sel = 0, nsel = 0, vt = 0, vf = 0, cond, nt, nf;
		bool et, ef;

		if (!t || V->p[t].in.op != VO_BRIF)
			continue;
		T = V->p[t].in.b, F = V->p[t].in.c;
		if (T == F)
			continue;
		cond = vw_find(V, V->p[t].in.a);
		nt = nf = 0;
		et = vw_armempty(V, T, b, cond, &nt), ef = vw_armempty(V, F, b, cond, &nf);
		if (et && ef && V->p[V->b[T].term].in.a == V->p[V->b[F].term].in.a)
			J = V->p[V->b[T].term].in.a;
		else if (et && V->p[V->b[T].term].in.a == F)
			J = F, ef = false;
		else if (ef && V->p[V->b[F].term].in.a == T)
			J = T, et = false;
		else
			continue;
		if (J == b || (et && ef && (J == T || J == F)))
			continue;
		it = vw_predix(V, J, et ? T : b), jf = vw_predix(V, J, ef ? F : b);
		if (it == VW_NOBLK || jf == VW_NOBLK || it == jf)
			continue;
		for (uint32_t p = V->b[J].phead; p; p = V->p[p].next) {
			uint32_t a, c;

			if (V->fwd[p] != p)
				continue;
			a = vw_find(V, V->x[V->p[p].in.b + it]), c = vw_find(V, V->x[V->p[p].in.b + jf]);
			if (a == c)
				continue;
			nsel++, sel = p, vt = a, vf = c;
		}
		if (nsel > 1 || (nsel && (V->p[sel].in.ty == VT_VOID || V->p[sel].in.ty == VT_MULTI || vt == sel || vf == sel)))
			continue;
		if (!nsel && (et ? nt : 0) + (ef ? nf : 0))	/* (nothing to select: the arms' work is dead or shared; leave it) */
			continue;
		if (et)
			vw_hoist(V, T, b, cond);
		if (ef)
			vw_hoist(V, F, b, cond);
		if (nsel) {
			uint32_t s = vw_ins_in(e, b, VO_SELECT, V->p[sel].in.ty, 0, cond, vt, vf);

			V->p[s].loc = V->p[t].loc;
			V->x[V->p[sel].in.b + it] = s;
		}
		/* J's entry `it` is b's now; entry jf goes */
		V->pr[V->b[J].pred + it] = b;
		for (uint32_t p = V->b[J].phead; p; p = V->p[p].next)
			if (V->p[p].in.b != VW_NOBLK)
				for (uint32_t k = jf; k + 1 < V->b[J].npred; k++)
					V->x[V->p[p].in.b + k] = V->x[V->p[p].in.b + k + 1];
		for (uint32_t k = jf; k + 1 < V->b[J].npred; k++)
			V->pr[V->b[J].pred + k] = V->pr[V->b[J].pred + k + 1];
		V->b[J].npred--;
		V->p[t].in = (vir_ins){ VO_BR, VT_VOID, 0, J, 0, 0 };
		if (et)
			V->b[T].reach = 0;
		if (ef)
			V->b[F].reach = 0;
		any = true;
	}
	if (any) {
		uint32_t m = 0;

		for (uint32_t i = 0; i < V->norder; i++)
			if (V->b[V->order[i]].reach)
				V->order[m++] = V->order[i];
		V->norder = m;
	}
	return any;
}

/*
 * Two branches to a common destination become one, as LLVM's SimplifyCFG
 * (FoldBranchToCommonDest): b ends in brif c1, S, O (or O, S) where S is b's alone,
 * holds one integer compare c2 and ends in brif c2 with O as one target.  Then b
 * computes c2 too and branches once on c1 and c2 (S on c1's true side) or c1 or c2
 * (false side), c2 inverted when O is S's true target; S goes.  O's parameters must
 * take the same values from b and from S.  The lowering turns the joined condition
 * into cmp + ccmp, so a compare that is a cbz or tbz on its own (against 0 or -1)
 * is left as its own branch.  Repeated: a && b && c is one chain.
 */
static bool vw_cbzlike(vwst *V, uint32_t c)
{
	const vir_ins *in = &V->p[c].in;
	int cc = in->fl & 15;
	int64_t k;

	if (!vw_kint(V, vw_find(V, in->b), &k))
		return false;
	return !k ? cc == 0 || cc == 1 || cc == 2 || cc == 5 || cc == 7 || cc == 8 : k == -1 && (cc == 3 || cc == 4);
}

/* v is a condition the lowering reads from the flags: a compare, or and/or of such a
 * condition and a compare; all in block b, each read once */
static bool vw_cchain(vwst *V, uint32_t v, const uint32_t *cnt, uint32_t b)
{
	for (;;) {
		const vwp *x = &V->p[v];
		uint32_t r;

		if (x->blk != b || cnt[v] != 1)
			return false;
		if (x->in.op == VO_ICMP)
			return !vw_cbzlike(V, v);
		if ((x->in.op != VO_AND && x->in.op != VO_OR) || x->in.ty != VT_I32)
			return false;
		r = vw_find(V, x->in.b);
		if (V->p[r].in.op != VO_ICMP || V->p[r].blk != b || cnt[r] != 1 || vw_cbzlike(V, r))
			return false;
		v = vw_find(V, x->in.a);
	}
}

static bool vw_condmerge(ecx *e)
{
	vwst *V = e->vw;
	uint32_t ncnt = V->np + V->norder + 1, *cnt = calloc(ncnt, sizeof *cnt);
	bool any = false, again = true;

	if (!cnt)
		vw_fail(e, "out of memory");
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p)
				VW_EACHOP(V, p, o, cnt[vw_find(V, *o)]++);
		for (uint32_t q = V->b[b].head; q; q = V->p[q].next)
			if (vw_find(V, q) == q && !vw_promdead(V, q))
				VW_EACHOP(V, q, o, cnt[vw_find(V, *o)]++);
		if (V->b[b].term)
			VW_EACHOP(V, V->b[b].term, o, cnt[vw_find(V, *o)]++);
	}
	while (again) {
		again = false;
		for (uint32_t i = 0; i < V->norder; i++) {
			static const uint8_t inv[10] = { 1, 0, 5, 4, 3, 2, 9, 8, 7, 6 };
			uint32_t b = V->order[i], t = V->b[b].term, S, O, X, ts, c1, c2, nlive = 0, ib, is, ix, n, prev;
			bool onfalse, flip;

			if (!V->b[b].reach || !t || V->p[t].in.op != VO_BRIF || V->p[t].in.b == V->p[t].in.c)
				continue;
			c1 = vw_find(V, V->p[t].in.a);
			for (int side = 0; side < 2; side++) {
				S = side ? V->p[t].in.c : V->p[t].in.b, O = side ? V->p[t].in.b : V->p[t].in.c;
				onfalse = side;
				ts = V->b[S].term;
				if (S == b || O == b || !V->b[S].reach || V->b[S].npred != 1 || V->pr[V->b[S].pred] != b || !ts ||
				    V->p[ts].in.op != VO_BRIF || V->p[ts].in.b == V->p[ts].in.c)
					continue;
				if (V->p[ts].in.b == O)
					X = V->p[ts].in.c, flip = !onfalse;	/* (and: S goes on when c2 fails; or: O when c2 holds) */
				else if (V->p[ts].in.c == O)
					X = V->p[ts].in.b, flip = onfalse;
				else
					continue;
				if (X == S || X == b)
					continue;
				{
					/* VCC_CCMP: `all` joins every such pair, `off` none.  The default joins only where
					 * the if-conversion can follow (X an empty arm into O, or both into one block: a
					 * value, not control flow).  Joining a plain branch pair costs the second compare
					 * whenever the first decided alone, and pays only when the branches mispredict:
					 * measured on sqlite, all: +0.4 % instructions, cycles unchanged (PLAN-vir.md). */
					static int mode = -1;
					uint32_t dn = 0;

					if (mode < 0) {
						const char *m = getenv("VCC_CCMP");

						mode = !m ? 1 : !strcmp(m, "off") ? 0 : !strcmp(m, "all") ? 2 : 1;
					}
					if (!mode)
						continue;
					if (mode == 1) {
						bool ex = vw_armempty(V, X, S, 0, &dn);
						uint32_t jx = ex ? V->p[V->b[X].term].in.a : VW_NOBLK;
						const vwb *ko = &V->b[O];

						bool eo = ko->term && V->p[ko->term].in.op == VO_BR && ko->npred == 2;

						for (uint32_t p = ko->phead; p && eo; p = V->p[p].next)
							eo = V->fwd[p] != p;
						for (uint32_t q = ko->head; q && eo; q = V->p[q].next)
							eo = vw_find(V, q) != q || vw_promdead(V, q) ||
							     (V->p[q].in.op == VO_MEMSET && (V->p[q].in.fl & VW_LIFETIME));
						if (!ex || (jx != O && !(eo && V->p[ko->term].in.a == jx)))
							continue;
					}
				}
				c2 = vw_find(V, V->p[ts].in.a);
				nlive = 0;
				for (uint32_t p = V->b[S].phead; p; p = V->p[p].next)
					nlive += V->fwd[p] == p;
				for (uint32_t q = V->b[S].head; q; q = V->p[q].next)
					if (vw_find(V, q) == q && !vw_promdead(V, q) &&
					    !(V->p[q].in.op == VO_MEMSET && (V->p[q].in.fl & VW_LIFETIME)))
						nlive += q == c2 ? 0 : 1;
				if (nlive || V->p[c2].in.op != VO_ICMP || V->p[c2].blk != S || cnt[c2] != 1 || vw_cbzlike(V, c2) ||
				    !vw_cchain(V, c1, cnt, b))
					continue;
				/* c1 is b's last instruction, or a compare that can move there */
				if (V->b[b].tail != c1 && V->p[c1].in.op != VO_ICMP)
					continue;
				ib = vw_predix(V, O, b), is = vw_predix(V, O, S), ix = vw_predix(V, X, S);
				if (ib == VW_NOBLK || is == VW_NOBLK || ix == VW_NOBLK || ib == is)
					continue;
				n = 0;
				for (uint32_t p = V->b[O].phead; p; p = V->p[p].next)
					if (V->fwd[p] == p && (V->p[p].in.b == VW_NOBLK ||
							       vw_find(V, V->x[V->p[p].in.b + ib]) != vw_find(V, V->x[V->p[p].in.b + is])))
						n++;
				if (n)
					continue;
				/* c1 to b's end, c2 after it, then the and/or */
				if (V->b[b].tail != c1) {
					prev = 0;
					for (uint32_t q = V->b[b].head; q != c1; q = V->p[q].next)
						prev = q;
					if (prev)
						V->p[prev].next = V->p[c1].next;
					else
						V->b[b].head = V->p[c1].next;
					V->p[V->b[b].tail].next = c1;
					V->p[c1].next = 0;
					V->b[b].tail = c1;
				}
				prev = 0;
				for (uint32_t q = V->b[S].head; q != c2; q = V->p[q].next)
					prev = q;
				if (prev)
					V->p[prev].next = V->p[c2].next;
				else
					V->b[S].head = V->p[c2].next;
				if (V->b[S].tail == c2)
					V->b[S].tail = prev;
				V->p[c2].blk = b, V->p[c2].next = 0;
				V->p[V->b[b].tail].next = c2;
				V->b[b].tail = c2;
				if (flip)
					V->p[c2].in.fl = (uint16_t)((V->p[c2].in.fl & ~15u) | inv[V->p[c2].in.fl & 15]);
				n = vw_ins_in(e, b, onfalse ? VO_OR : VO_AND, VT_I32, 0, c1, c2, 0);
				if (n >= ncnt)
					vw_fail(e, "VIR: joined conditions");
				V->p[n].loc = V->p[t].loc;
				cnt[n] = 1;
				V->p[t].in = onfalse ? (vir_ins){ VO_BRIF, VT_VOID, 0, n, O, X } : (vir_ins){ VO_BRIF, VT_VOID, 0, n, X, O };
				/* X is entered from b now; O's entry from S goes */
				V->pr[V->b[X].pred + ix] = b;
				for (uint32_t p = V->b[O].phead; p; p = V->p[p].next)
					if (V->p[p].in.b != VW_NOBLK)
						for (uint32_t k = is; k + 1 < V->b[O].npred; k++)
							V->x[V->p[p].in.b + k] = V->x[V->p[p].in.b + k + 1];
				for (uint32_t k = is; k + 1 < V->b[O].npred; k++)
					V->pr[V->b[O].pred + k] = V->pr[V->b[O].pred + k + 1];
				V->b[O].npred--;
				V->b[S].reach = 0;
				any = again = true;
				break;
			}
		}
	}
	free(cnt);
	if (any) {
		uint32_t m = 0;

		for (uint32_t i = 0; i < V->norder; i++)
			if (V->b[V->order[i]].reach)
				V->order[m++] = V->order[i];
		V->norder = m;
	}
	return any;
}

/* the edge b -> t needs a block of its own: t has parameters and b does not end in br */
static void vw_split(ecx *e)
{
	vwst *V = e->vw;
	uint32_t n0 = V->norder, nb0 = V->nb;	/* blocks from nb0 on are this pass's edge blocks */

	for (uint32_t i = 0; i < n0; i++) {
		uint32_t b = V->order[i], ns = vw_nsucc(V, b);

		if (V->p[V->b[b].term].in.op == VO_BR)
			continue;
		for (uint32_t k = 0; k < ns; k++) {
			uint32_t t = *vw_succp(V, b, k), live = 0, eb = VW_NOBLK;

			for (uint32_t p = V->b[t].phead; p; p = V->p[p].next)
				live += V->fwd[p] == p;
			if (!live)
				continue;
			/* one edge block per (b, t): t's predecessor b becomes it */
			for (uint32_t j = 0; j < V->b[t].npred; j++)
				if (V->pr[V->b[t].pred + j] == b) {
					eb = vw_newblk(e);
					V->b[eb].term = vw_put(e, VO_BR, VT_VOID, 0, t, 0, 0, eb);
					V->b[eb].reach = 1;
					GROW(V->pr, V->npr, V->cpr, 1);
					V->b[eb].pred = V->npr;
					V->pr[V->npr++] = b;
					V->b[eb].npred = 1;
					V->pr[V->b[t].pred + j] = eb;
					GROW(V->order, V->norder, V->corder, 1);
					V->order[V->norder++] = eb;
					break;
				}
			if (eb == VW_NOBLK) {	/* already split: the edge block whose br goes to t
						 * (not a block of the function that only branches there:
						 * switch cases 8, 9 -> t and 12 -> { op = 10; br t }) */
				for (uint32_t j = 0; j < V->b[t].npred; j++) {
					uint32_t q = V->pr[V->b[t].pred + j];

					if (q >= nb0 && V->b[q].npred == 1 && V->pr[V->b[q].pred] == b && !V->b[q].head &&
					    V->p[V->b[q].term].in.op == VO_BR && V->p[V->b[q].term].in.a == t)
						eb = q;
				}
			}
			*vw_succp(V, b, k) = eb;
		}
	}
	if (V->norder == n0)
		return;
	/* the edge blocks right after the block they leave (layout: a fall-through, not a jump back) */
	{
		uint32_t *o = malloc((size_t)V->norder * sizeof *o), m = 0;

		if (!o)
			vw_fail(e, "out of memory");
		for (uint32_t i = 0; i < n0; i++) {
			uint32_t b = V->order[i];

			o[m++] = b;
			for (uint32_t j = n0; j < V->norder; j++)
				if (V->pr[V->b[V->order[j]].pred] == b)
					o[m++] = V->order[j];
		}
		memcpy(V->order, o, (size_t)m * sizeof *o);
		free(o);
	}
}

/* pre-IR value v in VIR (constants made on first use) */
static uint32_t vw_m(ecx *e, uint32_t v)
{
	vwst *V = e->vw;
	vwp *q;

	v = vw_find(V, v);
	if (V->map[v])
		return V->map[v];
	q = &V->p[v];
	if (q->blk != VW_NOBLK) {
		V->why = "VIR: a value used before its definition";
		return 0;
	}
	if (q->in.op == VO_SLOTADDR) {
		vwslot *s = &V->sl[q->in.a];

		if (s->fin == VW_NOBLK)
			s->fin = vir_slot_add(&V->f, s->size, s->align, s->exposed ? 0 : 0, s->off);
		return V->map[v] = vir_const(&V->f, VO_SLOTADDR, VT_PTR, s->fin, q->in.b, q->in.c);
	}
	return V->map[v] = vir_const(&V->f, q->in.op, q->in.ty, q->in.a, q->in.b, q->in.c);
}

static uint32_t vw_fin_blk(vwst *V, uint32_t b) { return V->b[b].fin; }

static bool vw_emit_ins(ecx *e, uint32_t i, uint32_t b)
{
	vwst *V = e->vw;
	vir_fn *f = &V->f;
	vir_ins in = V->p[i].in;
	const vir_opinfo *oi = &vir_optab[in.op];
	uint32_t w[3] = { in.a, in.b, in.c };

	for (int k = 0; k < 3; k++)
		if (vw_isval(oi->k[k]) && !(in.op == VO_BRIF && k > 0))
			w[k] = vw_m(e, w[k]);
	switch (in.op) {
	case VO_CALL: {
		uint32_t n = V->x[in.c], o;

		w[1] = vir_extra(f, &V->x[in.b], 3 + 2 * VSIG_NGROUPS(V->x[in.b]));
		o = vir_extra(f, &V->x[in.c], 1 + n);
		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k] = vw_m(e, V->x[in.c + 1 + k]);
		w[2] = o;
		break;
	}
	case VO_RET: {
		uint32_t n = V->x[in.c], o = vir_extra(f, &V->x[in.c], 1 + n);

		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k] = vw_m(e, V->x[in.c + 1 + k]);
		w[2] = o;
		break;
	}
	case VO_BR: {
		uint32_t t = in.a, n = 0, j = 0, o = 0;

		while (j < V->b[t].npred && V->pr[V->b[t].pred + j] != b)
			j++;
		for (uint32_t p = V->b[t].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p) {
				uint32_t a = vw_m(e, V->x[V->p[p].in.b + j]);

				if (!n)
					o = vir_extra(f, &a, 1);
				else
					vir_extra(f, &a, 1);
				n++;
			}
		w[0] = vw_fin_blk(V, t);
		w[2] = n ? o : 0;
		break;
	}
	case VO_BRIF:
		w[1] = vw_fin_blk(V, in.b), w[2] = vw_fin_blk(V, in.c);
		break;
	case VO_SWITCH: {
		uint32_t n = V->x[in.c], st = V->p[in.a].in.ty == VT_I64 ? 5 : 3, o = vir_extra(f, &V->x[in.c], 1 + n * st);

		for (uint32_t k = 0; k < n; k++)
			f->extra[o + 1 + k * st + st - 1] = vw_fin_blk(V, V->x[in.c + 1 + k * st + st - 1]);
		w[1] = vw_fin_blk(V, in.b), w[2] = o;
		break;
	}
	default:
		break;
	}
	if (V->why)
		return false;
	{
		uint32_t v = vir_ins_add(f, in.op, in.ty, in.fl, w[0], w[1], w[2]);

		if (!v)
			return false;
		V->map[i] = v;
	}
	return true;
}

VPRIV bool vw_emit(ecx *e)
{
	vwst *V = e->vw;
	cc *c = e->c;
	vir_fn *f = &V->f;
	uint32_t lastloc = UINT32_MAX;

	vw_thread(e);
	vw_rpo(e);
	vw_preds(e);
	/* seal: every placeholder gets its operands, then trivial ones go */
	V->sealing = true;
	GROW(V->fwd, 0, V->cfwd, V->np + 1);
	for (uint32_t i = 0; i < V->np; i++)
		V->fwd[i] = i;
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->p[p].in.b == VW_NOBLK)
				vw_phiops(e, p, b);
	}
	vw_promote(e);
	while (vw_trivial(e))
		;
	vw_deadparams(e);
	vw_simplify(e);
	/* simplifying can free a slot's address (an inlined call's pointer argument parked as an
	   integer comes back as i2p(p2i $slot)): promote again, then simplify what it forwards */
	if (vw_promote(e)) {
		while (vw_trivial(e))
			;
		vw_deadparams(e);
		vw_simplify(e);
	}
	if (vw_condmerge(e))
		while (vw_trivial(e))
			;
	if (vw_ifconv(e))
		while (vw_trivial(e))
			;
	vw_split(e);
	/* stores to a slot that is not exposed and that nothing reads are dead */
	for (uint32_t s = 0; s < V->nsl; s++)
		V->sl[s].used = 0, V->sl[s].fin = VW_NOBLK;
	for (uint32_t i = 0; i < V->norder; i++)
		for (uint32_t q = V->b[V->order[i]].head; q; q = V->p[q].next)
			if (V->p[q].in.op == VO_LOAD && V->p[q].slot)
				V->sl[V->p[q].slot - 1].used = 1;
	/* emission */
	vir_begin(f, DARWIN(c) ? VTG_A64_DARWIN : VTG_A64_LINUX, REF(RK_SYM, e->wk->fsym),
		  (T(c, c->syms[e->wk->fsym].ty)->flags & TF_VARIADIC) ? VF_VARIADIC : 0);
	GROW(V->map, 0, V->cmap, V->np + 1);
	memset(V->map, 0, (size_t)V->np * sizeof *V->map);
	for (uint32_t i = 0; i < V->norder; i++)
		V->b[V->order[i]].fin = i;
	f->sig = vir_extra(f, &V->x[V->fsig], 3 + 2 * VSIG_NGROUPS(V->x[V->fsig]));
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		if (vir_blk_add(f, 0) == VIR_NOBLK)
			return false;
		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p && !(V->map[p] = vir_ins_add(f, VO_BPARAM, V->p[p].in.ty, 0, 0, 0, 0)))
				return false;
		for (uint32_t q = V->b[b].head; q; q = V->p[q].next) {
			vwp *x = &V->p[q];

			if (vw_find(V, q) != q)
				continue;
			if (vw_promdead(V, q) || (x->in.op == VO_MEMSET && (x->in.fl & VW_LIFETIME)) ||
			    (x->in.op == VO_STORE && x->slot && !(x->in.fl & VIF_VOL) && !V->sl[x->slot - 1].exposed &&
			     !V->sl[x->slot - 1].used))
				continue;	/* (a dead store) */
			if (x->loc != lastloc) {
				vir_loc_add(f, x->loc);
				lastloc = x->loc;
			}
			if (!vw_emit_ins(e, q, b))
				return false;
		}
		if (!V->b[b].term) {
			V->why = "VIR: a block without a terminator";
			return false;
		}
		if (!vw_emit_ins(e, V->b[b].term, b))
			return false;
	}
	if (vir_failed())
		return false;
	return vir_canon(f);
}
