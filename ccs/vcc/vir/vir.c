/*
 * VIR: the thread's memory, construction, the analyses (cfg, dominators,
 * use counts), the canonical form, and the unit's tables (SPEC-vir.md 2-3,
 * 22).
 */
#include "virimpl.h"

/* ---------------- the thread's reservations (22.2) ---------------- */

static _Thread_local vir_ctx *tl_ctx;

vir_ctx *vir_ctx_get(void)
{
	vir_ctx *c = tl_ctx;

	if (c)
		return c;
	c = calloc(1, sizeof(*c));
	if (!c)
		abort();
	for (int i = 0; i < 2; i++) {
		c->ins[i] = vreserve((size_t)VIR_MAX_INS * sizeof(vir_ins), MAP_NORESERVE, "vir: ins");
		c->blk[i] = vreserve((size_t)VIR_MAX_BLK * sizeof(vir_blk), MAP_NORESERVE, "vir: blk");
		c->loc[i] = vreserve((size_t)VIR_MAX_LOC * sizeof(vir_loc), MAP_NORESERVE, "vir: loc");
	}
	c->slot = vreserve((size_t)VIR_MAX_SLOT * sizeof(vir_slot), MAP_NORESERVE, "vir: slot");
	c->extra = vreserve((size_t)VIR_MAX_EXTRA * 4, MAP_NORESERVE, "vir: extra");
	c->tmp = vreserve((size_t)VIR_MAX_INS * 4, MAP_NORESERVE, "vir: tmp");
	c->aux = vreserve((size_t)VIR_MAX_INS * 4, MAP_NORESERVE, "vir: aux");
	c->ctab = vreserve(((size_t)1 << VIR_CTAB_BITS) * sizeof(*c->ctab), MAP_NORESERVE, "vir: constants");
	for (int i = 0; i < 6; i++)
		c->bs[i] = vreserve((size_t)VIR_MAX_BLK * 4, MAP_NORESERVE, "vir: block scratch");
	c->sc = vreserve((size_t)VIR_MAX_EXTRA * 4, MAP_NORESERVE, "vir: successors");
	tl_ctx = c;
	return c;
}

bool vir_failed(void) { return tl_ctx && tl_ctx->fail; }

/* a new generation: every table entry written before is empty (22.3) */
VPRIV void vir_ctab_reset(vir_ctx *c)
{
	if (++c->gen == 0) {
		memset(c->ctab, 0, ((size_t)1 << VIR_CTAB_BITS) * sizeof(*c->ctab));
		c->gen = 1;
	}
	c->ncon = 0;
}

static inline uint32_t chash(uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	uint64_t h = ((uint64_t)op << 8 | ty) * 0x9e3779b97f4a7c15ull;

	h = (h ^ a) * 0xff51afd7ed558ccdull;
	h = (h ^ b) * 0xc4ceb9fe1a85ec53ull;
	h = (h ^ c) * 0x9e3779b97f4a7c15ull;
	return (uint32_t)(h >> 40) & (((uint32_t)1 << VIR_CTAB_BITS) - 1);
}

static inline bool cmatch(const vir_ins *x, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	return x->op == op && x->ty == ty && x->a == a && x->b == b && x->c == c;
}

/* the entry for a key: its value, or 0 and *slot at the empty entry where it goes */
VPRIV uint32_t vir_cfind(vir_ctx *x, const vir_fn *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c,
		      uint32_t *slot)
{
	uint32_t m = ((uint32_t)1 << VIR_CTAB_BITS) - 1, h = chash(op, ty, a, b, c);

	while (x->ctab[h].gen == x->gen) {
		uint32_t v = x->ctab[h].val;

		if (cmatch(&f->ins[v], op, ty, a, b, c))
			return v;
		h = (h + 1) & m;
	}
	*slot = h;
	return 0;
}

/* the table of the constants [1, nconst) of f (after canon, a load, or the verifier) */
VPRIV void vir_ctab_rebuild(vir_fn *f)
{
	vir_ctx *x = vir_ctx_get();

	vir_ctab_reset(x);
	for (uint32_t j = 1; j < f->nconst && j < f->nins; j++) {
		const vir_ins *c = &f->ins[j];
		uint32_t slot = 0;

		if (vir_is_const_op(c->op) && !vir_cfind(x, f, c->op, c->ty, c->a, c->b, c->c, &slot) && x->ncon < VIR_MAX_CONST) {
			x->ctab[slot].gen = x->gen;
			x->ctab[slot].val = j;
			x->ncon++;
		}
	}
}

/* ---------------- construction ---------------- */

void vir_begin(vir_fn *f, uint32_t target, uint32_t sym, uint32_t fl)
{
	vir_ctx *c = vir_ctx_get();

	memset(f, 0, sizeof(*f));
	c->fail = false;
	c->cur = 0;
	c->blkcur = c->loccur = 0;
	c->cfg_mark = c->cfg_end = 0;
	vir_ctab_reset(c);
	f->version = 1;
	f->target = target;
	f->sym = sym;
	f->fl = fl;
	f->ins = c->ins[0];
	f->blk = c->blk[0];
	f->slot = c->slot;
	f->extra = c->extra;
	f->loc = c->loc[0];
	f->tmp = c->tmp;
	memset(&f->ins[0], 0, sizeof(vir_ins));	/* index 0: a nop */
	f->nins = 1;
	f->extra[0] = 0;	/* offset 0 means "none" */
	f->nextra = 1;
}

static inline bool open_block(const vir_fn *f) { return f->nblk != 0; }

static uint32_t ins_push(vir_fn *f, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c)
{
	vir_ctx *x = vir_ctx_get();
	vir_ins *p;

	if (f->nins >= VIR_MAX_INS) {
		x->fail = true;
		return 0;
	}
	p = &f->ins[f->nins];
	p->op = (uint8_t)op;
	p->ty = (uint8_t)ty;
	p->fl = (uint16_t)fl;
	p->a = a;
	p->b = b;
	p->c = c;
	if (open_block(f))
		f->blk[f->nblk - 1].n++;
	return f->nins++;
}

uint32_t vir_const_raw(vir_fn *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	return ins_push(f, op, ty, 0, a, b, c);
}

uint32_t vir_const(vir_fn *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c)
{
	vir_ctx *x = vir_ctx_get();
	uint32_t slot = 0, v = vir_cfind(x, f, op, ty, a, b, c, &slot);

	if (v)
		return v;
	if (x->ncon >= VIR_MAX_CONST) {
		x->fail = true;
		return 0;
	}
	v = ins_push(f, op, ty, 0, a, b, c);
	if (!v)
		return 0;
	x->ctab[slot].gen = x->gen;
	x->ctab[slot].val = v;
	x->ncon++;
	return v;
}

uint32_t vir_blk_add(vir_fn *f, uint16_t fl)
{
	vir_ctx *x = vir_ctx_get();
	vir_blk *b;

	if (f->nblk >= VIR_MAX_BLK) {
		x->fail = true;
		return VIR_NOBLK;
	}
	b = &f->blk[f->nblk];
	memset(b, 0, sizeof(*b));
	b->first = f->nins;
	b->fl = fl;
	b->idom = VIR_NOBLK;
	return f->nblk++;
}

uint32_t vir_ins_add(vir_fn *f, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c)
{
	vir_blk *k;
	uint32_t v;

	if (!f->nblk) {
		vir_ctx_get()->fail = true;
		return 0;
	}
	k = &f->blk[f->nblk - 1];
	if (op == VO_BPARAM && k->nparam == k->n) {
		if (k->nparam >= VIR_MAX_BPARAM) {	/* (the count would wrap: a br's list has its length) */
			vir_ctx_get()->fail = true;
			return 0;
		}
		k->nparam++;
	}
	v = ins_push(f, op, ty, fl, a, b, c);
	if (!v && op == VO_BPARAM && k->nparam > k->n)
		k->nparam--;
	return v;
}

uint32_t vir_slot_add(vir_fn *f, uint64_t size, uint32_t align, uint32_t fl, int64_t hint)
{
	vir_slot *s;

	if (f->nslot >= VIR_MAX_SLOT) {
		vir_ctx_get()->fail = true;
		return VIR_NOBLK;
	}
	s = &f->slot[f->nslot];
	s->size = size;
	s->align = align;
	s->fl = fl;
	s->hint = hint;
	return f->nslot++;
}

uint32_t vir_extra(vir_fn *f, const uint32_t *w, uint32_t n)
{
	uint32_t off = f->nextra;

	if (n > VIR_MAX_EXTRA - off) {
		vir_ctx_get()->fail = true;
		return 0;
	}
	if (n)
		memcpy(&f->extra[off], w, (size_t)n * 4);
	f->nextra += n;
	return off;
}

void vir_loc_add(vir_fn *f, uint32_t loc)
{
	if (f->nloc && f->loc[f->nloc - 1].loc == loc)
		return;
	if (f->nloc >= VIR_MAX_LOC) {
		vir_ctx_get()->fail = true;
		return;
	}
	f->loc[f->nloc].first = f->nins;
	f->loc[f->nloc].loc = loc;
	f->nloc++;
}

/* ---------------- operand and block-reference iteration ---------------- */

static inline bool is_value_kind(uint32_t k)
{
	return (k >= OK_T && k <= OK_BITC) || k == OK_CONSTV;
}

void vir_oit_init(vir_oit *it, vir_fn *f, vir_ins *in, bool blocks)
{
	const vir_opinfo *oi = &vir_optab[in->op];
	uint32_t op = in->op, ne = f->nextra;

	memset(it, 0, sizeof(*it));
	it->f = f;
	it->in = in;
	it->stride = 1;
	if (!oi->name || op >= VO_TARGET0)
		return;
	for (int k = 0; k < 3; k++)
		if (blocks ? oi->k[k] == OK_BLK : is_value_kind(oi->k[k]))
			it->fm |= 1u << k;
	/* extra lists: n words at base (each a pointer-sized group, stride apart) */
	if (!blocks) {
		if ((op == VO_CALL || op == VO_RET) && in->c < ne) {
			uint32_t n = f->extra[in->c];

			if ((uint64_t)in->c + 1 + n <= ne) {
				it->base = in->c + 1;
				it->n = n;
			}
		} else if (op == VO_BR) {
			uint32_t n = in->a < f->nblk ? f->blk[in->a].nparam : 0;

			if ((uint64_t)in->c + n <= ne) {
				it->base = in->c;
				it->n = n;
			}
		} else if ((op == VO_ASM || op == VO_ASMGOTO) && (uint64_t)in->b + 2 <= ne) {
			uint32_t no = f->extra[in->b], ni = f->extra[in->b + 1];

			if ((uint64_t)in->b + 2 + no + 2 * (uint64_t)ni <= ne) {
				it->base = in->b + 2 + no + 1;	/* the first input's value */
				it->n = ni;
				it->stride = 2;
			}
		}
	} else if (op == VO_SWITCH && in->c < ne) {
		uint32_t n = f->extra[in->c], ty = in->a < f->nins ? f->ins[in->a].ty : 0;
		uint32_t st = ty == VT_I32 ? 3 : ty == VT_I64 ? 5 : 0;

		if (st && (uint64_t)in->c + 1 + (uint64_t)n * st <= ne) {
			it->base = in->c + 1 + st - 1;
			it->n = n;
			it->stride = st;
		}
	} else if (op == VO_IBR && in->c < ne) {
		uint32_t n = f->extra[in->c];

		if ((uint64_t)in->c + 1 + n <= ne) {
			it->base = in->c + 1;
			it->n = n;
		}
	} else if (op == VO_ASMGOTO && in->c < ne) {
		uint32_t n = f->extra[in->c];

		if ((uint64_t)in->c + 2 + n <= ne) {
			it->base = in->c + 1;
			it->n = n + 1;	/* the fallthrough block, then the labels */
		}
	}
}

uint32_t *vir_oit_next(vir_oit *it)
{
	while (it->fi < 3) {
		uint32_t k = it->fi++;

		if (it->fm >> k & 1) {
			it->pos = k;
			return k == 0 ? &it->in->a : k == 1 ? &it->in->b : &it->in->c;
		}
	}
	if (it->i < it->n) {
		it->pos = 3;
		return &it->f->extra[it->base + (it->i++) * it->stride];
	}
	return NULL;
}

/* ---------------- control-flow graph ---------------- */

/* index of the last real instruction of block b: what is in its range that is not a constant created inside it */
VPRIV uint32_t vir_term_of(const vir_fn *f, uint32_t b)
{
	const vir_blk *k = &f->blk[b];

	for (uint32_t i = k->first + k->n; i > k->first; i--)
		if (!vir_is_const_op(f->ins[i - 1].op))
			return i - 1;
	return 0;
}

/* successor lists of every block (CSR: succ[off[b] .. off[b+1])); false if a target is out of range or a range is */
VPRIV bool vir_succ_csr(vir_fn *f, uint32_t *off, uint32_t *succ)
{
	uint32_t e = 0;

	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *k = &f->blk[b];
		uint32_t t;

		off[b] = e;
		if ((uint64_t)k->first + k->n > f->nins || !k->n)
			continue;
		t = vir_term_of(f, b);
		if (t && vir_is_term(f->ins[t].op)) {
			vir_ins x = f->ins[t];
			vir_oit it;
			uint32_t *p;

			vir_oit_init(&it, f, &x, true);
			while ((p = vir_oit_next(&it))) {
				if (*p >= f->nblk || e >= VIR_MAX_EXTRA)
					return false;
				succ[e++] = *p;
			}
		}
	}
	off[f->nblk] = e;
	return true;
}

bool vir_cfg(vir_fn *f)
{
	vir_ctx *x = vir_ctx_get();
	uint32_t *off = x->bs[0], *cnt = x->bs[1], *succ = x->sc, ne, n = f->nblk;

	if (x->cfg_end == f->nextra && x->cfg_end > x->cfg_mark)
		f->nextra = x->cfg_mark;	/* the lists of the last call: replaced */
	x->cfg_mark = x->cfg_end = f->nextra;
	if (!vir_succ_csr(f, off, succ))
		return false;
	ne = off[n];
	if (ne > VIR_MAX_EXTRA - f->nextra)
		return false;
	memset(cnt, 0, (size_t)n * 4);
	for (uint32_t i = 0; i < ne; i++)
		cnt[succ[i]]++;
	for (uint32_t b = 0, p = f->nextra; b < n; b++) {
		f->blk[b].pred = p;
		f->blk[b].npred = 0;
		p += cnt[b];
	}
	for (uint32_t b = 0; b < n; b++)
		for (uint32_t i = off[b]; i < off[b + 1]; i++) {
			vir_blk *t = &f->blk[succ[i]];

			f->extra[t->pred + t->npred++] = b;
		}
	f->nextra += ne;
	x->cfg_end = f->nextra;
	return true;
}

/* reverse postorder from block 0 into rpo (successors visited last to first, so that
 * the first successor comes first); returns the count of reachable blocks */
static uint32_t rpo_order(const vir_fn *f, const uint32_t *off, const uint32_t *succ, uint32_t *rpo, uint32_t *stk,
			  uint32_t *pos)
{
	uint32_t n = f->nblk, sp = 0, np = 0;

	if (!n)
		return 0;
	for (uint32_t b = 0; b < n; b++)
		pos[b] = VIR_NOBLK;
	/* pos[] doubles as the visited mark (1 on the stack) while the DFS runs */
	stk[sp++] = 0;
	pos[0] = off[1] - off[0];	/* successors left to visit */
	while (sp) {
		uint32_t b = stk[sp - 1];

		if (pos[b]) {
			uint32_t s = succ[off[b] + --pos[b]];

			if (pos[s] == VIR_NOBLK) {
				pos[s] = off[s + 1] - off[s];
				stk[sp++] = s;
			}
		} else {
			rpo[np++] = b;	/* postorder for now */
			pos[b] = 0;
			sp--;
		}
	}
	for (uint32_t i = 0; i < np / 2; i++) {
		uint32_t t = rpo[i];

		rpo[i] = rpo[np - 1 - i];
		rpo[np - 1 - i] = t;
	}
	for (uint32_t b = 0; b < n; b++)
		pos[b] = VIR_NOBLK;
	for (uint32_t i = 0; i < np; i++)
		pos[rpo[i]] = i;
	return np;
}

bool vir_dom(vir_fn *f)
{
	vir_ctx *x = vir_ctx_get();
	uint32_t n = f->nblk, np, *rpo = x->bs[2], *stk = x->bs[3], *rn = x->bs[4], *id = x->bs[5];
	uint32_t *off = x->bs[0], *succ = x->sc, *sz = x->bs[1], *nx = x->bs[3];
	bool ch = true;

	if (!n || !vir_cfg(f))
		return false;
	/* vir_cfg left the successors in off/succ; stk and nx share bs[3] (the DFS is over) */
	np = rpo_order(f, off, succ, rpo, stk, rn);
	for (uint32_t b = 0; b < n; b++)
		id[b] = VIR_NOBLK;
	id[0] = 0;
	while (ch) {	/* Cooper, Harvey and Kennedy */
		ch = false;
		for (uint32_t i = 1; i < np; i++) {
			uint32_t b = rpo[i], ni = VIR_NOBLK;
			const vir_blk *k = &f->blk[b];

			for (uint32_t j = 0; j < k->npred; j++) {
				uint32_t p = f->extra[k->pred + j];

				if (id[p] == VIR_NOBLK)
					continue;
				if (ni == VIR_NOBLK) {
					ni = p;
					continue;
				}
				{
					uint32_t a = p, c = ni;

					while (a != c) {
						while (rn[a] > rn[c])
							a = id[a];
						while (rn[c] > rn[a])
							c = id[c];
					}
					ni = a;
				}
			}
			if (id[b] != ni) {
				id[b] = ni;
				ch = true;
			}
		}
	}
	/* subtree sizes, then preorder numbers: a child's range is inside its parent's */
	for (uint32_t b = 0; b < n; b++)
		sz[b] = 1;
	for (uint32_t i = np; i-- > 1;)
		sz[id[rpo[i]]] += sz[rpo[i]];
	for (uint32_t b = 0; b < n; b++) {
		f->blk[b].idom = VIR_NOBLK;
		f->blk[b].pre = f->blk[b].post = VIR_NOBLK;
	}
	f->blk[0].idom = 0;
	f->blk[0].pre = 0;
	f->blk[0].post = sz[0] - 1;
	nx[0] = 1;
	for (uint32_t i = 1; i < np; i++) {
		uint32_t b = rpo[i], p = id[b];

		f->blk[b].idom = p;
		f->blk[b].pre = nx[p];
		nx[p] += sz[b];
		nx[b] = f->blk[b].pre + 1;
		f->blk[b].post = f->blk[b].pre + sz[b] - 1;
	}
	return true;
}

void vir_uses(const vir_fn *f, uint8_t *cnt)
{
	memset(cnt, 0, f->nins);
	for (uint32_t b = 0; b < f->nblk; b++) {
		const vir_blk *k = &f->blk[b];

		for (uint32_t i = k->first; i < k->first + k->n && i < f->nins; i++) {
			vir_ins x = f->ins[i];
			vir_oit it;
			uint32_t *p;

			if (vir_is_const_op(x.op))
				continue;
			vir_oit_init(&it, (vir_fn *)f, &x, false);
			while ((p = vir_oit_next(&it)))
				if (*p < f->nins && cnt[*p] != 255)
					cnt[*p]++;
		}
	}
}

/* ---------------- the canonical form (2.4, 22.5) ---------------- */

/* the source location of old instruction index i (the run that holds it), or 0xffffffff */
static uint32_t loc_at(const vir_loc *l, uint32_t n, uint32_t i)
{
	uint32_t lo = 0, hi = n;

	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;

		if (l[m].first <= i)
			lo = m + 1;
		else
			hi = m;
	}
	return lo ? l[lo - 1].loc : 0xffffffffu;
}

bool vir_canon(vir_fn *f)
{
	vir_ctx *x = vir_ctx_get();
	uint32_t n = f->nblk, np, *rpo, *order, nord = 0, *tmp = f->tmp, *nb;
	vir_ins *dst;
	vir_blk *nbk;
	vir_loc *nl;
	uint32_t nc = 1, k;
	bool keep = true;

	if (!n || !f->nins || !vir_dom(f))
		return false;
	rpo = x->bs[2];
	np = 0;
	for (uint32_t b = 0; b < n; b++)
		np += f->blk[b].idom != VIR_NOBLK;
	/* keep the array order when every block comes after its immediate dominator */
	for (uint32_t b = 1; b < n; b++)
		if (f->blk[b].idom != VIR_NOBLK && f->blk[b].idom > b)
			keep = false;
	order = x->aux;	/* [0, np): the new order of blocks; it is not used as a value array until pass 2 */
	nb = x->bs[0];	/* old block id -> new, VIR_NOBLK if dropped (off[] is dead now) */
	for (uint32_t b = 0; b < n; b++)
		nb[b] = VIR_NOBLK;
	if (keep) {
		for (uint32_t b = 0; b < n; b++)
			if (f->blk[b].idom != VIR_NOBLK)
				order[nord++] = b;
	} else {
		for (uint32_t i = 0; i < np; i++)
			order[nord++] = rpo[i];
	}
	for (uint32_t i = 0; i < nord; i++)
		nb[order[i]] = i;
	/* pass 1: the constants in order of first use (duplicates unified), written to dst */
	dst = x->ins[x->cur ^ 1];
	memset(&dst[0], 0, sizeof(vir_ins));
	memset(tmp, 0xff, (size_t)f->nins * 4);
	tmp[0] = 0;
	vir_ctab_reset(x);
	{
		vir_fn g = *f;	/* the table is searched in dst's numbering */

		g.ins = dst;
		for (uint32_t i = 0; i < nord; i++) {
			const vir_blk *kb = &f->blk[order[i]];

			if ((uint64_t)kb->first + kb->n > f->nins)
				return false;
			for (uint32_t q = kb->first; q < kb->first + kb->n; q++) {
				vir_ins y = f->ins[q];
				vir_oit it;
				uint32_t *p;

				if (y.op == VO_NOP || vir_is_const_op(y.op))
					continue;
				vir_oit_init(&it, f, &y, false);
				while ((p = vir_oit_next(&it))) {
					uint32_t v = *p;

					if (v >= f->nins || tmp[v] != 0xffffffffu || !vir_is_const_op(f->ins[v].op))
						continue;
					{
						vir_ins cc = f->ins[v];
						uint32_t slot = 0, h;

						if (cc.op == VO_LABELADDR) {
							if (cc.a >= n || nb[cc.a] == VIR_NOBLK)
								return false;
							cc.a = nb[cc.a];
						}
						h = vir_cfind(x, &g, cc.op, cc.ty, cc.a, cc.b, cc.c, &slot);
						if (!h) {
							if (nc >= VIR_MAX_CONST)
								return false;
							h = nc++;
							dst[h] = cc;
							dst[h].fl = 0;
							x->ctab[slot].gen = x->gen;
							x->ctab[slot].val = h;
							x->ncon++;
						}
						tmp[v] = h;
					}
				}
			}
		}
	}
	/* pass 2: the block instructions, renumbered; lists are rewritten in place */
	nbk = x->blk[x->blkcur ^ 1];
	nl = x->loc[x->loccur ^ 1];
	k = nc;
	{
		uint32_t nnl = 0;

		for (uint32_t i = 0; i < nord; i++) {
			const vir_blk *kb = &f->blk[order[i]];
			vir_blk *o = &nbk[i];
			uint32_t first = k;

			*o = *kb;
			o->first = first;
			o->nparam = 0;
			o->npred = o->pred = 0;
			for (uint32_t q = kb->first; q < kb->first + kb->n; q++) {
				vir_ins y = f->ins[q], z;
				vir_oit iv, ib;
				uint32_t *p;

				if (y.op == VO_NOP || vir_is_const_op(y.op))
					continue;
				if (k >= VIR_MAX_INS)
					return false;
				z = y;
				vir_oit_init(&iv, f, &z, false);
				vir_oit_init(&ib, f, &z, true);
				while ((p = vir_oit_next(&iv))) {
					uint32_t v = *p;

					*p = v < f->nins && tmp[v] != 0xffffffffu ? tmp[v] : 0;
				}
				while ((p = vir_oit_next(&ib)))
					*p = *p < n && nb[*p] != VIR_NOBLK ? nb[*p] : 0;
				if (z.op == VO_BPARAM && o->nparam == k - first)
					o->nparam++;
				tmp[q] = k;
				dst[k] = z;
				if (f->nloc && nl) {
					uint32_t l = loc_at(f->loc, f->nloc, q);

					if (l != 0xffffffffu && (!nnl || nl[nnl - 1].loc != l) && nnl < VIR_MAX_LOC) {
						nl[nnl].first = k;
						nl[nnl].loc = l;
						nnl++;
					}
				}
				k++;
			}
			o->n = k - first;
		}
		f->nloc = nnl;
	}
	/* a block (and the constants) may only mention what is renumbered: operands defined later are 0 */
	f->ins = dst;
	f->blk = nbk;
	f->loc = nl;
	x->cur ^= 1;
	x->blkcur ^= 1;
	x->loccur ^= 1;
	f->nins = k;
	f->nconst = nc;
	f->nblk = nord;
	vir_ctab_rebuild(f);
	f->nextra = x->cfg_mark;	/* the old predecessor lists */
	x->cfg_end = x->cfg_mark;
	return vir_dom(f);
}

/* ---------------- units ---------------- */

void vir_unit_init(vir_unit *u) { memset(u, 0, sizeof(*u)); }

VPRIV void *vir_unit_alloc(vir_unit *u, size_t n)
{
	void *p = malloc(n ? n : 1);

	if (!p)
		abort();
	if (u->nheap == u->cheap) {
		u->cheap = u->cheap ? u->cheap * 2 : 64;
		u->heap = realloc(u->heap, u->cheap * sizeof(void *));
		if (!u->heap)
			abort();
	}
	u->heap[u->nheap++] = p;
	return p;
}

VPRIV const char *vir_unit_str(vir_unit *u, const char *s, size_t n)
{
	char *p = vir_unit_alloc(u, n + 1);

	memcpy(p, s, n);
	p[n] = 0;
	return p;
}

#define UGROW(arr, n, cap) do { \
	if ((n) == (cap)) { \
		(cap) = (cap) ? (cap) * 2 : 16; \
		(arr) = realloc((arr), (size_t)(cap) * sizeof(*(arr))); \
		if (!(arr)) abort(); \
	} \
} while (0)

void vir_unit_free(vir_unit *u)
{
	for (uint32_t i = 0; i < u->nfn; i++) {
		free(u->fn[i].ins);
		free(u->fn[i].blk);
		free(u->fn[i].slot);
		free(u->fn[i].extra);
		free(u->fn[i].loc);
	}
	for (uint32_t i = 0; i < u->nheap; i++)
		free(u->heap[i]);
	free(u->heap);
	free(u->sym);
	free(u->sig);
	free(u->sigw);
	free(u->as);
	free(u->item);
	free(u->fn);
	free(u->fsym);
	memset(u, 0, sizeof(*u));
}

uint32_t vir_unit_sym(vir_unit *u, const char *name, size_t n)
{
	for (uint32_t i = 0; i < u->nsym; i++)
		if (strlen(u->sym[i].name) == n && !memcmp(u->sym[i].name, name, n))
			return i;
	UGROW(u->sym, u->nsym, u->csym);
	memset(&u->sym[u->nsym], 0, sizeof(vir_usym));
	u->sym[u->nsym].name = vir_unit_str(u, name, n);
	u->sym[u->nsym].kind = VU_UNDECL;
	return u->nsym++;
}

uint32_t vir_unit_sigw(vir_unit *u, const uint32_t *w, uint32_t n)
{
	for (uint32_t i = 0; i < u->nsig; i++)
		if (u->sig[i].n == n && !memcmp(&u->sigw[u->sig[i].off], w, (size_t)n * 4))
			return i;
	while (u->nsigw + n > u->csigw) {
		u->csigw = u->csigw ? u->csigw * 2 : 64;
		u->sigw = realloc(u->sigw, (size_t)u->csigw * 4);
		if (!u->sigw)
			abort();
	}
	UGROW(u->sig, u->nsig, u->csig);
	memcpy(&u->sigw[u->nsigw], w, (size_t)n * 4);
	u->sig[u->nsig].off = u->nsigw;
	u->sig[u->nsig].n = n;
	u->nsigw += n;
	return u->nsig++;
}

static void *dup_arr(const void *p, size_t n)
{
	void *q = malloc(n ? n : 1);

	if (!q)
		abort();
	if (n)
		memcpy(q, p, n);
	return q;
}

static void fn_copy(vir_fn *d, const vir_fn *f)
{
	*d = *f;
	d->ins = dup_arr(f->ins, (size_t)f->nins * sizeof(vir_ins));
	d->blk = dup_arr(f->blk, (size_t)f->nblk * sizeof(vir_blk));
	d->slot = dup_arr(f->slot, (size_t)f->nslot * sizeof(vir_slot));
	d->extra = dup_arr(f->extra, (size_t)f->nextra * 4);
	d->loc = dup_arr(f->loc, (size_t)f->nloc * sizeof(vir_loc));
	d->tmp = NULL;
}

void vir_fn_save(vir_unit *u, const vir_fn *f, uint32_t sym)
{
	UGROW(u->fn, u->nfn, u->cfn);
	u->fsym = realloc(u->fsym, (size_t)u->cfn * sizeof(uint32_t));
	if (!u->fsym)
		abort();
	fn_copy(&u->fn[u->nfn], f);
	u->fsym[u->nfn] = sym;
	u->nfn++;
}

void vir_fn_store(vir_unit *u, uint32_t i, const vir_fn *f)
{
	vir_fn *d = &u->fn[i];

	free(d->ins);
	free(d->blk);
	free(d->slot);
	free(d->extra);
	free(d->loc);
	fn_copy(d, f);
}

void vir_fn_load(vir_fn *f, const vir_unit *u, uint32_t i)
{
	const vir_fn *s = &u->fn[i];

	vir_begin(f, s->target, s->sym, s->fl);
	f->version = s->version;
	f->sig = s->sig;
	memcpy(f->ins, s->ins, (size_t)s->nins * sizeof(vir_ins));
	memcpy(f->blk, s->blk, (size_t)s->nblk * sizeof(vir_blk));
	memcpy(f->slot, s->slot, (size_t)s->nslot * sizeof(vir_slot));
	memcpy(f->extra, s->extra, (size_t)s->nextra * 4);
	memcpy(f->loc, s->loc, (size_t)s->nloc * sizeof(vir_loc));
	f->nins = s->nins;
	f->nconst = s->nconst;
	f->nblk = s->nblk;
	f->nslot = s->nslot;
	f->nextra = s->nextra;
	f->nloc = s->nloc;
	vir_ctab_rebuild(f);
}
