/*
 * vir_alloc_check (SPEC-vir.md 15.11): the allocation, checked by a forward
 * symbolic walk that knows nothing of how it was made.
 *
 * A location is a register byte (0-255: the two classes and the scratch
 * registers) or a spill slot (256 + slot).  The state maps each location to
 * the value it holds, 0 for none.  Effects, in the order lowering performs
 * them (15.3): a bparam with a register and a slot is stored at the top of
 * its block; the edits at an instruction; the operands are read (each must be
 * in its register, or for an operand the target lets be memory or an
 * immediate, a constant or in its slot); the clobbers; the result written to
 * its register and, when the value has a slot, stored there.  An edge applies
 * its moves in order, then each block parameter's location must hold its
 * argument and is renamed to the parameter.  Block entry states meet by
 * intersection (a location keeps a value only if every incoming edge agrees)
 * and are iterated to a fixed point; the checks run once more on the final
 * states, so a failure means some path reads a wrong value.
 *
 * Also checked: fixed operand and result registers, early clobbers, the
 * two-address rule (a result in a register other than the first operand's must
 * not be another operand's, since lowering copies the first operand there
 * first), registers are allocatable, and every callee-saved register written
 * is in the output's set.  Not a fast path: it allocates with calloc.
 */
#include "virimpl.h"
#include "vir/alloc.h"

typedef struct chk {
	vir_fn *f;
	const vir_regs *rg;
	const vir_alloc_out *o;
	vbuffer *msg;
	uint32_t nloc, nerr, *nm;
	uint64_t cwrote[2];
	bool on;		/* report (the last round) */
} chk;

#define SLOC(s) (256u + (s))

static void vname(const chk *c, char *o, uint32_t v)
{
	const vir_ins *in = &c->f->ins[v];

	if (v < c->f->nconst) {
		if (in->op == VO_ICONST || in->op == VO_FCONST)
			snprintf(o, 40, "%s 0x%llx", vir_optab[in->op].name, (unsigned long long)((uint64_t)in->c << 32 | in->b));
		else
			snprintf(o, 40, "%s(%u)", vir_optab[in->op].name, in->a);
	} else if (c->nm[v] != VIR_NOBLK) {
		snprintf(o, 40, "v%u", c->nm[v]);
	} else {
		snprintf(o, 40, "#%u", v);
	}
}

static void locname(char *o, uint32_t l)
{
	if (l >= 256)
		snprintf(o, 16, "ss%u", l - 256);
	else
		vir_reg_name(o, l);
}

__attribute__((format(printf, 4, 5)))
static void bad(chk *c, uint32_t b, uint32_t i, const char *fmt, ...)
{
	char t[300];
	va_list ap;

	if (!c->on)
		return;
	if (c->nerr++ >= 10)
		return;
	va_start(ap, fmt);
	vsnprintf(t, sizeof(t), fmt, ap);
	va_end(ap);
	if (i)
		vbuffer_format_append(c->msg, "alloc-check: b%u ins %u: %s\n", b, i, t);
	else
		vbuffer_format_append(c->msg, "alloc-check: b%u: %s\n", b, t);
}

static bool reg_ok(const chk *c, uint32_t r, bool scratch)
{
	if (r == VR_SCR0 || r == VR_SCR1)
		return scratch;
	return r < 128 && (c->rg->alloc[VR_CLASS(r)] >> VR_NUM(r) & 1);
}

static void wrote(chk *c, uint32_t r)
{
	if (r < 128)
		c->cwrote[VR_CLASS(r)] |= 1ull << VR_NUM(r);
}

/* the location an operand of the edge or block-top logic means for value v in register byte r */
static uint32_t ploc(const chk *c, uint32_t q, uint8_t r)
{
	if (r != VR_NONE)
		return r;
	return c->o->slot[q] == VIR_NOBLK ? VIR_NOBLK : SLOC(c->o->slot[q]);
}

static bool apply_move(chk *c, uint32_t *st, const vir_emove *m, uint32_t b)
{
	uint32_t d = m->dst, s = m->src;
	char t[40];

	d = d & VL_SLOT ? SLOC(d & ~VL_SLOT) : d;
	s = s & VL_SLOT ? SLOC(s & ~VL_SLOT) : s;
	if (d >= c->nloc || (m->kind != VE_REMAT && s >= c->nloc) || (d < 256 && !reg_ok(c, d, true))) {
		bad(c, b, 0, "edge move with a bad location");
		return false;
	}
	wrote(c, d);
	switch (m->kind) {
	case VE_MOVE:
		st[d] = st[s];
		break;
	case VE_REMAT:
		if (m->v >= c->f->nconst || !m->v) {
			vname(c, t, m->v);
			bad(c, b, 0, "edge rematerializes %s, not a constant", t);
		}
		st[d] = m->v;
		break;
	case VE_XCHG: {
		uint32_t x = st[d];

		wrote(c, s);
		st[d] = st[s];
		st[s] = x;
		break;
	}
	default:
		bad(c, b, 0, "edge move of kind %u", m->kind);
		return false;
	}
	return true;
}

/* one block from its entry state (in st, changed); calls meet for each edge */
static void block(chk *c, uint32_t b, uint32_t *st, uint32_t *tmp, uint32_t **ent, bool *seen, uint32_t *wl, uint32_t *nwl,
		  bool *inwl)
{
	vir_fn *f = c->f;
	const vir_alloc_out *o = c->o;
	const vir_blk *k = &f->blk[b];
	uint32_t pos = o->boff[b], e = o->boff[f->nblk + 1 + b], eend = o->boff[f->nblk + 2 + b];
	uint32_t ops[512];
	char t1[40], t2[40], t3[16];

	for (uint32_t j = 0; j < k->nparam; j++) {
		uint32_t q = k->first + j;
		uint8_t r = o->opr[pos++];

		if (r != VR_NONE && o->slot[q] != VIR_NOBLK && o->sdef[q])
			st[SLOC(o->slot[q])] = st[r] == q ? q : 0;	/* the store at the top */
	}
	for (uint32_t i = k->first + k->nparam; i < k->first + k->n; i++) {
		vir_ins in = f->ins[i];
		vir_fix fx;
		vir_oit it;
		uint32_t *p, n = 0;
		bool swapped = false;
		uint8_t rr[512];

		for (; e < eend && o->edit[e].pos == i; e++) {
			const vir_edit *d = &o->edit[e];

			if (d->kind == VE_STORE) {
				if (d->v >= f->nins || o->slot[d->v] == VIR_NOBLK) {
					vname(c, t1, d->v);
					bad(c, b, i, "store of %s, which has no slot", t1);
				} else if (!reg_ok(c, d->src, false)) {
					bad(c, b, i, "store from register byte %u", d->src);
				} else {
					/* on a path where v is not defined (15.5: liveness over loop ranges
					 * keeps it live there) the register holds something else: the slot
					 * then holds nothing, which only a read can get wrong */
					st[SLOC(o->slot[d->v])] = st[d->src] == d->v ? d->v : 0;
				}
				continue;
			}
			if (d->kind != VE_SWAP && !reg_ok(c, d->dst, false)) {
				bad(c, b, i, "edit writes register byte %u", d->dst);
				continue;
			}
			switch (d->kind) {
			case VE_RELOAD:
				if (d->v >= f->nins || o->slot[d->v] == VIR_NOBLK) {
					vname(c, t1, d->v);
					bad(c, b, i, "reload of %s, which has no slot", t1);
					st[d->dst] = 0;
				} else {
					st[d->dst] = st[SLOC(o->slot[d->v])];
				}
				wrote(c, d->dst);
				break;
			case VE_REMAT:
				if (!d->v || d->v >= f->nconst)
					bad(c, b, i, "rematerializes a non-constant");
				st[d->dst] = d->v;
				wrote(c, d->dst);
				break;
			case VE_MOVE:
				if (!reg_ok(c, d->src, false))
					bad(c, b, i, "move from register byte %u", d->src);
				else
					st[d->dst] = st[d->src];
				wrote(c, d->dst);
				break;
			case VE_SWAP:
				swapped = true;
				break;
			default:
				bad(c, b, i, "edit of kind %u", d->kind);
			}
		}
		vir_fix_clear(&fx);
		if (!c->rg->opfix || c->rg->opfix[in.op])
			c->rg->fixed(f, i, &fx, c->rg->ctx);
		vir_oit_init(&it, f, &in, false);
		while ((p = vir_oit_next(&it)) && n < 512) {
			if (f->ins[*p].ty == VT_MULTI)
				continue;	/* a call's results are read by its projs, from registers */
			ops[n] = *p;
			rr[n] = o->opr[pos++];
			n++;
		}
		if (in.op != VO_BR)
			for (uint32_t j = 0; j < n; j++) {
				uint32_t v = ops[j], r = rr[j];
				bool fixd = j < fx.nopnd && fx.opnd[j] != VR_NONE;
				bool mem = (j < 32 && (fx.mem >> j & 1)) || (j >= fx.nopnd && (fx.fl & VFX_RESTMEM));

				vname(c, t1, v);
				if (r == VR_NONE) {
					if (!mem)
						bad(c, b, i, "operand %u (%s) has no register", j, t1);
					else if (v >= f->nconst && (o->slot[v] == VIR_NOBLK || st[SLOC(o->slot[v])] != v))
						bad(c, b, i, "memory operand %u: %s is not in its slot", j, t1);
					continue;
				}
				if (!reg_ok(c, r, false)) {
					bad(c, b, i, "operand %u in register byte %u", j, r);
					continue;
				}
				if (st[r] != v) {
					vir_reg_name(t3, r);
					if (st[r])
						vname(c, t2, st[r]);
					else
						strcpy(t2, "nothing");
					bad(c, b, i, "operand %u: %s holds %s, not %s", j, t3, t2, t1);
				}
				if (fixd && r != fx.opnd[j]) {
					vir_reg_name(t3, fx.opnd[j]);
					bad(c, b, i, "operand %u must be in %s", j, t3);
				}
				if (!fixd && (fx.eclob[VR_CLASS(r)] >> VR_NUM(r) & 1))
					bad(c, b, i, "operand %u is in an early-clobbered register", j);
			}
		for (int cl = 0; cl < 2; cl++) {
			uint64_t m = fx.clob[cl] | fx.eclob[cl];

			while (m) {
				uint32_t r = VR_MAKE(cl, __builtin_ctzll(m));

				m &= m - 1;
				st[r] = 0;
				wrote(c, r);
			}
		}
		if (in.ty != VT_VOID && in.ty != VT_MULTI) {
			uint8_t r = o->opr[pos++];

			if (fx.res != VR_NONE && r != VR_NONE && r != fx.res) {
				vir_reg_name(t3, fx.res);
				bad(c, b, i, "result must be in %s", t3);
			}
			if (r != VR_NONE) {
				if (!reg_ok(c, r, false) || VR_CLASS(r) != vir_rclass(in.ty)) {
					bad(c, b, i, "result in register byte %u", r);
				} else {
					if ((fx.fl & VFX_TIED) && n >= 1) {
						uint32_t first = swapped && n >= 2 ? 1 : 0;

						if (r != rr[first])
							for (uint32_t j = 0; j < n; j++)
								if (j != first && rr[j] == r)
									bad(c, b, i, "two-address result in operand %u's register", j);
					}
					st[r] = i;
					wrote(c, r);
				}
			}
			if (o->slot[i] != VIR_NOBLK && o->sdef[i]) {
				if (r == VR_NONE)
					bad(c, b, i, "a value with a slot is defined without a register");
				st[SLOC(o->slot[i])] = r == VR_NONE ? 0 : i;
			}
		}
		if (swapped && !((fx.fl & VFX_COMMUT) && n >= 2))
			bad(c, b, i, "operands swapped on a non-commutative instruction");
		if (i + 1 == k->first + k->n) {	/* the terminator: the edges */
			vir_oit bi;
			uint32_t kk = 0;

			vir_oit_init(&bi, f, &in, true);
			while ((p = vir_oit_next(&bi))) {
				uint32_t to = *p, ei = o->eidx[o->soff[b] + kk];
				const vir_blk *tk = &f->blk[to];

				memcpy(tmp, st, (size_t)c->nloc * 4);
				if (ei != VIR_NOBLK)
					for (uint32_t m = 0; m < o->edge[ei].n; m++)
						apply_move(c, tmp, &o->move[o->edge[ei].first + m], b);
				for (uint32_t j = 0; j < tk->nparam; j++) {
					uint32_t q = tk->first + j, l = ploc(c, q, o->opr[o->boff[to] + j]);
					uint32_t a = in.op == VO_BR ? f->extra[in.c + j] : 0;

					if (l == VIR_NOBLK)
						continue;	/* unused parameter: nowhere */
					if (tmp[l] != a) {
						vname(c, t1, q);
						vname(c, t2, a);
						locname(t3, l);
						bad(c, b, 0, "edge to b%u: %s (%s) does not hold the argument %s", to, t1, t3, t2);
					}
					tmp[l] = q;
				}
				/* meet */
				{
					uint32_t *en = ent[to];
					bool ch = false;

					if (!seen[to]) {
						memcpy(en, tmp, (size_t)c->nloc * 4);
						seen[to] = ch = true;
					} else {
						for (uint32_t l = 0; l < c->nloc; l++)
							if (en[l] && en[l] != tmp[l]) {
								en[l] = 0;
								ch = true;
							}
					}
					if (ch && !inwl[to]) {
						inwl[to] = true;
						wl[(*nwl)++] = to;
					}
				}
				kk++;
			}
		}
	}
}

uint32_t vir_alloc_check(vir_fn *f, const vir_regs *rg, const vir_alloc_out *o, vbuffer *msg)
{
	chk c = { f, rg, o, msg, 256 + o->nspill, 0, NULL, { 0, 0 }, false };
	uint32_t nb = f->nblk, *wl, nwl = 0, *st, *tmp, **ent, n = 0;
	bool *seen, *inwl;

	if (!nb)
		return 0;
	c.nm = calloc(f->nins, 4);
	ent = calloc(nb, sizeof(*ent));
	seen = calloc(nb, 1);
	inwl = calloc(nb, 1);
	wl = calloc(nb, 4);
	st = calloc(c.nloc, 4);
	tmp = calloc(c.nloc, 4);
	if (!c.nm || !ent || !seen || !inwl || !wl || !st || !tmp)
		abort();
	for (uint32_t i = 0; i < f->nins; i++)
		c.nm[i] = VIR_NOBLK;
	for (uint32_t b = 0; b < nb; b++) {
		for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n; i++)
			if (f->ins[i].ty != VT_VOID)
				c.nm[i] = n++;
		ent[b] = calloc(c.nloc, 4);
		if (!ent[b])
			abort();
	}
	seen[0] = inwl[0] = true;
	wl[nwl++] = 0;
	while (nwl) {	/* LIFO; terminates because states only lose entries */
		uint32_t b = wl[--nwl];

		inwl[b] = false;
		memcpy(st, ent[b], (size_t)c.nloc * 4);
		block(&c, b, st, tmp, ent, seen, wl, &nwl, inwl);
	}
	c.on = true;
	c.cwrote[0] = c.cwrote[1] = 0;
	for (uint32_t b = 0; b < nb; b++) {
		if (!seen[b]) {
			bad(&c, b, 0, "not reached");
			continue;
		}
		memcpy(st, ent[b], (size_t)c.nloc * 4);
		block(&c, b, st, tmp, ent, seen, wl, &nwl, inwl);
	}
	for (int cl = 0; cl < 2; cl++) {
		uint64_t m = c.cwrote[cl] & rg->callee[cl] & ~o->callee[cl];

		if (m)
			bad(&c, 0, 0, "callee-saved %s%d written but not in the used set", cl ? "f" : "r", __builtin_ctzll(m));
	}
	if (c.nerr > 10)
		vbuffer_format_append(msg, "alloc-check: ... %u failures in all\n", c.nerr);
	for (uint32_t b = 0; b < nb; b++)
		free(ent[b]);
	free(ent);
	free(seen);
	free(inwl);
	free(wl);
	free(st);
	free(tmp);
	free(c.nm);
	return c.nerr;
}
