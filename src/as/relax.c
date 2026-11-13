/* relax.c: variable-size items (.prefalign, alignment after one, LEB128 of a label difference, .skip, x86-64 jumps) and their relaxation at the end of assembly. */
#include "asimpl.h"

/* After relaxation, debug encoders only read the layout until they append
 * their completed table. Cache the offsets once for that immutable interval. */
VPRIV void frag_bases(ElfObj *o)
{
	for (uint32_t s = 1; s < o->nsec; s++) {
		uint64_t off = 0;
		for (uint32_t f = s; f; f = elf_frag(o, f)->next) {
			ElfFrag *p = elf_frag(o, f);
			p->base = off;
			off += p->size;
		}
	}
}

/* ---------------------------------------------------------------- .prefalign */


/* close the current frag with a variable-size item; what follows goes to a new frag */
VPRIV struct VarItem *var_item(As *a, int kind)
{
	if (a->npfa == a->pfacap) {
		uint32_t nc = a->pfacap ? a->pfacap * 2 : 64;
		void *t = rget(&a->defs, sizeof *a->pfa * nc);
		if (a->npfa) memcpy(t, a->pfa, sizeof *a->pfa * a->npfa);
		a->pfa = t; a->pfacap = nc;
	}
	struct VarItem *v = &a->pfa[a->npfa++];
	memset(v, 0, sizeof *v);
	v->kind = (uint8_t)kind; v->frag = a->cur;
	a->cur = elf_split(a->o, a->cur);
	return v;
}

/* an x86-64 jump to v (cc 16: jmp): short while a local target in this section is
 * in range, else the near form with a fixup (MC's relaxation) */
VPRIV int as_branch(As *a, int cc, Val v, int mod)
{
	struct VarItem *it;

	(void)mod;
	elf_code(a->o, a->cur);
	fold(a, &v);
	elf_reg(a->o, v.add);	/* (the symbol table's order: where the fixup would be made, MC) */
	elf_reg(a->o, v.sub);
	it = var_item(a, VI_X86BR);
	it->code = (uint8_t)cc;
	it->v = v;
	it->size = 2;
	return 0;
}

/* .prefalign p2, end, nop|fill: align to min(2^p2, the power of two that
 * holds [here, end)) (MC) */
VPRIV int d_prefalign(As *a, const char *p, const char *e)
{
	int64_t p2, fill = 0;
	if (abs_expr(a, &p, e, &p2)) return -1;
	if (p2 < 0 || p2 > 16) return err(a, "invalid alignment");
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected a comma");
	Sv nm = first_word(ws(p + 1, e), e);
	if (!nm.n) return err(a, "expected symbol name");
	uint32_t y = sym(a, nm.s, nm.n);
	p = ws(nm.s + nm.n, e);
	int nop = 0;
	if (p < e && *p == ',') {
		p = ws(p + 1, e);
		if (e - p >= 3 && !memcmp(p, "nop", 3)) { nop = 1; p += 3; }
		else if (abs_expr(a, &p, e, &fill)) return -1;
	}
	struct VarItem *v = var_item(a, VI_PREFALIGN);
	v->end = y; v->p2 = (uint8_t)p2; v->code = (uint8_t)nop; v->fill = (uint8_t)fill;
	return 0;
}

/* sizes before the variable items are final: offsets with the current estimates */
static _Thread_local const uint64_t *var_prev;	/* sizes of the previous relaxation pass (MC's stale offsets), or NULL */

/* Section ids grow upwards, continuation/subsection ids downwards. Keep the
 * index dense without allocating a table for the unused reservation. */
static uint32_t var_index(ElfObj *o, uint32_t f)
{
	return f < o->nsec ? f : o->nsec + (uint32_t)(o->frags.cap / sizeof(ElfFrag)) - 1 - f;
}

static uint64_t var_sum(As *a, uint32_t i)
{
	uint64_t sum = 0;
	for (; i; i &= i - 1) sum += a->var_tree[i];
	return sum;
}

static void var_add(As *a, uint32_t i, uint64_t delta)
{
	/* Unsigned wrap also represents a size decrease. */
	for (; i <= a->var_n; i += i & -i) a->var_tree[i] += delta;
}

static uint64_t frag_base(As *a, uint32_t frag)
{
	ElfObj *o = a->o;
	if (a->var_rank) {
		uint32_t rank = a->var_rank[var_index(o, frag)];
		if (var_prev) return a->var_oldoff[rank];
		uint32_t first = a->var_rank[var_index(o, elf_frag(o, frag)->sec)];
		return var_sum(a, rank - 1) - var_sum(a, first - 1);
	}
	uint64_t off = 0;
	for (uint32_t f = elf_frag(o, frag)->sec; f && f != frag; f = elf_frag(o, f)->next) {
		off += elf_frag(o, f)->size;
		if (elf_frag(o, f)->cont && elf_frag(o, f)->aux)
			off += var_prev ? var_prev[elf_frag(o, f)->aux - 1] : a->pfa[elf_frag(o, f)->aux - 1].size;
	}
	return off;
}



static int sym_secoff(As *a, uint32_t y, uint32_t *sec, int64_t *off)
{
	ElfSym *s = elf_symp(a->o, y);
	if (!(s->flags & ESYM_DEFINED) || (s->flags & ESYM_ABS)) return -1;
	*sec = elf_frag(a->o, s->frag)->sec;
	*off = (int64_t)(frag_base(a, s->frag) + s->value);
	return 0;
}

static int fits_i8(int64_t v) { return v >= -128 && v <= 127; }

static int leb_len(int64_t v, int sgn)
{
	uint8_t t[16]; Buf8 b = { t, 0 };
	if (sgn) sleb(&b, v); else uleb(&b, (uint64_t)v);
	return (int)b.n;
}

/* v under the current layout: label differences in one section, deferred expressions */
static int lay_eval(As *a, Val v, int64_t *out, int depth)
{
	int64_t x = v.c, k[2] = { 0, 0 };
	uint32_t y[2] = { v.add, v.sub }, sc[2] = { 0, 0 };
	int rel[2] = { 0, 0 };
	if (depth > 64) return -1;
	for (int i = 0; i < 2; i++) {
		if (!y[i]) continue;
		ElfSym *s = elf_symp(a->o, y[i]);
		int op; ElfVal ex, ey;
		if (s->flags & ESYM_ABS) k[i] = (int64_t)s->value;
		else if (elf_expr_get(a->o, y[i], &op, &ex, &ey)) {
			int64_t p, q;
			if (lay_eval(a, ex, &p, depth + 1) || lay_eval(a, ey, &q, depth + 1) || !elf_expr_op(op, p, q, &k[i])) return -1;
		} else if (!sym_secoff(a, y[i], &sc[i], &k[i])) rel[i] = 1;
		else return -1;
	}
	if (rel[0] != rel[1] || (rel[0] && sc[0] != sc[1])) return -1;
	*out = x + k[0] - k[1];
	return 0;
}

static int var_eval(As *a, struct VarItem *v, int64_t *val)
{
	int64_t x = v->v.c, o1, o2;
	uint32_t s1 = 0, s2 = 0;
	if (v->v.add && sym_secoff(a, v->v.add, &s1, &o1)) return -1;
	if (v->v.sub && sym_secoff(a, v->v.sub, &s2, &o2)) return -1;
	if (v->v.add && v->v.sub && s1 != s2) return -1;
	if (v->v.add && !v->v.sub) return -1;
	if (v->v.add) x += o1 - o2;
	*val = x;
	return 0;
}

/* MC's relaxation: recompute every size until none changes, then emit */
VPRIV void var_finish(As *a)
{
	ElfObj *o = a->o;
	for (uint32_t i = 0; i < a->npfa; i++) elf_frag(o, a->pfa[i].frag)->aux = i + 1;
	uint32_t count = o->nsec + (uint32_t)(o->frags.cap / sizeof(ElfFrag)) - 1 - o->nfrag;
	a->var_rank = rget(&a->defs, sizeof(uint32_t) * count);
	a->var_tree = rget(&a->defs, sizeof(uint64_t) * count);
	a->var_oldoff = rget(&a->defs, sizeof(uint64_t) * count);
	memset(a->var_tree, 0, sizeof(uint64_t) * count);
	a->var_n = count - 1;
	uint32_t rank = 0;
	for (uint32_t s = 1; s < o->nsec; s++)
		for (uint32_t f = s; f; f = elf_frag(o, f)->next) {
			ElfFrag *p = elf_frag(o, f);
			a->var_rank[var_index(o, f)] = ++rank;
			var_add(a, rank, p->size + (p->cont && p->aux ? a->pfa[p->aux - 1].size : 0));
		}
	uint64_t *prev = rget(&a->defs, sizeof(uint64_t) * (a->npfa + 1));
	/* pass -1 lays out the items with every jump short (MC's first layout); then each pass
	   relaxes the jumps against the previous pass's layout and lays the rest out again */
	for (int it = -1;; it++) {
		int changed = 0;
		for (uint32_t i = 0; i < a->npfa; i++) prev[i] = a->pfa[i].size;
		/* Jumps see the whole previous pass, while alignments/LEBs see the
		 * live layout, including size changes earlier in this pass. */
		for (uint32_t s = 1; s < o->nsec; s++) {
			uint64_t off = 0;
			for (uint32_t f = s; f; f = elf_frag(o, f)->next) {
				ElfFrag *p = elf_frag(o, f);
				a->var_oldoff[a->var_rank[var_index(o, f)]] = off;
				off += p->size + (p->cont && p->aux ? prev[p->aux - 1] : 0);
			}
		}
		for (uint32_t i = 0; i < a->npfa; i++) {
			struct VarItem *v = &a->pfa[i];
			uint64_t end = frag_base(a, v->frag) + elf_frag(o, v->frag)->size, ns = 0;
			if (v->kind == VI_PREFALIGN || v->kind == VI_ALIGN) {
				uint64_t al = (uint64_t)1 << v->p2;
				if (v->kind == VI_PREFALIGN) {
					uint32_t nf = elf_frag(o, v->frag)->cont, sc; int64_t eo;
					if (sym_secoff(a, v->end, &sc, &eo) || sc != elf_frag(o, v->frag)->sec) { err(a, ".prefalign: the end symbol must follow in the same section"); return; }
					uint64_t size = (uint64_t)eo - frag_base(a, nf);
					al = 1;
					while (al < size && al < ((uint64_t)1 << v->p2)) al <<= 1;
					if (o->machine == EM_X86_64) {	/* x86: never lowered (a size and an alignment inside can oscillate) */
						if (al < (uint64_t)v->v.c) al = (uint64_t)v->v.c;
						v->v.c = (int64_t)al;
					}
				}
				ns = (al - (end & (al - 1))) & (al - 1);
				if (v->max && ns > v->max) ns = 0;
				if (v->kind == VI_PREFALIGN) {
					ElfSec *s = elf_sec(o, elf_frag(o, v->frag)->sec);
					unsigned lg = (unsigned)__builtin_ctzll(al);
					if (lg > s->align) s->align = lg;
				}
			} else if (v->kind == VI_SKIP) {
				int64_t x;
				var_prev = prev;
				int r = lay_eval(a, v->v, &x, 0);
				var_prev = NULL;
				if (r) { err(a, "expected absolute expression"); return; }
				ns = x > 0 ? (uint64_t)x : 0;
			} else if (v->kind == VI_X86BR) {
				/* the short form while the target is a local label of this section within reach */
				uint64_t big = v->code == 16 ? 5 : 6;
				Val t = v->v;
				uint32_t sc; int64_t to;

				fold(a, &t);
				ns = v->size;
				if (ns == 2 && it >= 0) {
					ElfSym *y = t.add ? elf_symp(o, t.add) : NULL;
					int r;

					uint64_t pend;

					/* every jump against the previous pass's layout, its own place too (MC's
					   relaxOnce relaxes a section's fragments, then lays it out again) */
					var_prev = prev;
					pend = frag_base(a, v->frag) + elf_frag(o, v->frag)->size;
					r = !y || t.sub || y->bind != STB_LOCAL || sym_secoff(a, t.add, &sc, &to) || sc != elf_frag(o, v->frag)->sec;
					var_prev = NULL;
					if (r || !fits_i8(to + t.c - (int64_t)(pend + 2)))
						ns = big;
				}
			} else {
				int64_t x;
				if (var_eval(a, v, &x)) { err(a, "expected absolute expression"); return; }
				ns = (uint64_t)leb_len(x, v->kind == VI_SLEB);
				if (ns < v->size) ns = v->size;	/* never shrink: converges */
			}
			if (ns != v->size) {
				if (elf_frag(o, v->frag)->cont)
					var_add(a, a->var_rank[var_index(o, v->frag)], ns - v->size);
				v->size = ns; changed = 1;
			}
		}
		if (!changed && it >= 0) break;
		if (it == 64) { err(a, "relaxation did not converge"); return; }
	}
	for (uint32_t i = 0; i < a->npfa; i++) {
		struct VarItem *v = &a->pfa[i];
		uint64_t pad = v->size;
		elf_frag(o, v->frag)->aux = 0;
		if (!pad) continue;
		uint64_t qoff = elf_frag(o, v->frag)->size;
		uint8_t *q = elf_grow(o, v->frag, pad);
		if (v->kind == VI_X86BR) {
			Val t = v->v;

			fold(a, &t);
			if (pad == 2) {
				uint32_t sc; int64_t to = 0;

				sym_secoff(a, t.add, &sc, &to);
				q[0] = (uint8_t)(v->code == 16 ? 0xeb : 0x70 + v->code);
				q[1] = (uint8_t)(to + t.c - (int64_t)(frag_base(a, v->frag) + qoff + 2));
				continue;
			}
			if (v->code == 16) { q[0] = 0xe9; }
			else { q[0] = 0x0f; q[1] = (uint8_t)(0x80 + v->code); }
			memset(q + pad - 4, 0, 4);
			t.c -= 4;
			elf_fixup(o, v->frag, qoff + pad - 4, q + pad - 4, R_X86_64_PLT32, t);	/* (MC: every branch) */
			continue;
		}
		if (v->kind == VI_ULEB || v->kind == VI_SLEB) {
			int64_t x; var_eval(a, v, &x);
			uint8_t t[16]; Buf8 b = { t, 0 };
			if (v->kind == VI_SLEB) sleb(&b, x); else uleb(&b, (uint64_t)x);
			if (b.n < pad) {	/* padded to the relaxed size (MC: padding bytes 0x80 ..) */
				t[b.n - 1] |= 0x80;
				while (b.n < pad - 1) t[b.n++] = v->kind == VI_SLEB && x < 0 ? 0xff : 0x80;
				t[b.n++] = v->kind == VI_SLEB && x < 0 ? 0x7f : 0;
			}
			memcpy(q, t, pad);
			continue;
		}
		if (!v->code || v->kind == VI_SKIP) { memset(q, v->fill, pad); continue; }
		if (o->machine == EM_X86_64) { elf_x86_nops(q, pad); continue; }
		uint64_t z = pad & 3;
		memset(q, 0, z);
		for (q += z, pad -= z; pad; pad -= 4, q += 4) { q[0] = 0x1f; q[1] = 0x20; q[2] = 0x03; q[3] = 0xd5; }
	}
	a->var_rank = NULL;
}
