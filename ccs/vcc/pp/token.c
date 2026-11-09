/* vcc preprocessor: token contexts: the stack of token sources pp_next reads from */
#include "ppimpl.h"

/* ---------------- tokens ---------------- */

_Static_assert(offsetof(vtok, id) == offsetof(vtok, len) + 4 && offsetof(ptok, id) == offsetof(ptok, len) + 4 &&
	       offsetof(vtok, flags) == offsetof(vtok, kind) + 1 && offsetof(ptok, kind) == offsetof(ptok, loc) + 4 &&
	       offsetof(ptok, flags) == offsetof(ptok, kind) + 1 && offsetof(ptok, ctr) == offsetof(ptok, kind) + 2 &&
	       sizeof(vtok) == offsetof(vtok, kind) + 4, "vfill's layout");

static inline void mkeof(ptok *t)
{
	memset(t, 0, sizeof(*t));
	t->kind = T_EOF;
}

static size_t splice_at(const char *s, size_t i, size_t n)
{
	size_t j;

	if (s[i] != '\\')
		return 0;
	for (j = i + 1; j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\f' || s[j] == '\v'); j++)
		;
	if (j < n && s[j] == '\n')
		return j + 1 - i;
	if (j < n && s[j] == '\r')
		return j + 1 - i + (j + 1 < n && s[j + 1] == '\n');
	return 0;
}

const char *pp_spell(const ptok *t, char *buf, size_t *len)
{
	size_t n = 0, l;

	if (!(t->flags & TF_DIRTY)) {
		*len = t->len;
		return t->s;
	}
	for (size_t i = 0; i < t->len;) {
		if ((l = splice_at(t->s, i, t->len))) {
			i += l;
			continue;
		}
		buf[n++] = t->s[i++];
	}
	*len = n;
	return buf;
}

/* clean spelling appended to dst; returns the new end */
VPRIV char *spell_to(char *dst, const ptok *t)
{
	size_t n;
	const char *s = pp_spell(t, dst, &n);

	if (s != dst)
		memcpy(dst, s, n);
	return dst + n;
}

/* sv[base..svn) to tmp; svn = base */
VPRIV ptok *svtake(pp *p, size_t base, uint32_t *n)
{
	size_t k = p->svn - base;
	ptok *r = varena_alloc(&p->tmp, (k ? k : 1) * sizeof(ptok));

	memcpy(r, p->sv + base, k * sizeof(ptok));
	p->svn = base;
	*n = (uint32_t)k;
	return r;
}

VPRIV void push_ptok(pp *p, const ptok *t, uint32_t n, uint32_t mid, uint8_t first, int barrier)
{
	ctx *c = push(p);

	c->type = C_PTOK;
	c->pt = t;
	c->n = n;
	c->mid = mid;
	c->first = first;
	c->barrier = (uint8_t)barrier;
}

/* tokens [b, e) of the current directive line as a barrier context */
VPRIV void push_line(pp *p, const ctx *fc, uint32_t b, uint32_t e)
{
	ctx *c = push(p);

	c->type = C_VTOK;
	c->vt = fc->vt + b;
	c->n = e > b ? e - b : 0;
	c->src = fc->src;
	c->loc = fc->loc;
	c->offloc = 1;
	c->barrier = 1;
}

/* pop everything down to and including the barrier context at depth d */
VPRIV void pop_to(pp *p, int d)
{
	while (p->depth > d) {
		ctx *c = &p->cx[p->depth - 1];

		if (c->type == C_FILE) {
			p->nfile--;
		} else if (c->mid) {
			menable(p, c->mid);
		}
		p->depth--;
	}
}

/* a top-level region event (pp_top_events) at the current delivered position */
VPRIV void top_event(pp *p, const void *e, uint32_t base, unsigned ctr, uint8_t end, uint8_t hit)
{
	if (p->tev_full)
		return;
	if (!p->tev)
		p->tev = varena_alloc(&p->perm, 1024 * sizeof(pp_topev));
	if (p->ntev == 1024) {
		p->tev_full = 1;
		return;
	}
	p->tev[p->ntev++] = (pp_topev){ e, p->ntok_out, base, ctr, end, hit };
}

/* the next token, no macro expansion; runs directives in file contexts */
VPRIV __attribute__((noinline)) void raw(pp *p, ptok *t)
{
	for (;;) {
		ctx *c = &p->cx[p->depth - 1];

		if (likely(c->i < c->n)) {
			if (c->type == C_PTOK) {
				*t = c->pt[c->i++];
			} else if (c->type == C_VTOK) {
				const vtok *v = &c->vt[c->i++];

				vfill(t, v, c->src, c->offloc ? c->loc + v->off : c->loc);
			} else if (c->type == C_REPLAY) {
				if (replay_seg(p, c))
					continue;
				replay_tok(p, c, t);
			} else {
				const vtok *v = &c->vt[c->i];

				if (unlikely((v->flags & TF_SOL) && v->kind == T_HASH)) {
					if (directive(p, c, t))
						return;
					continue;
				}
				c->i++;
				vfill(t, v, c->src, c->loc + v->off);
			}
			if (c->first) {
				t->flags = (t->flags & ~(TF_SOL | TF_SPACE)) | (c->first & 3);
				c->first = 0;
			}
			if (p->pend) {
				t->flags |= p->pend;
				p->pend = 0;
			}
			return;
		}
		if (c->type == C_REPLAY) {
			if (replay_seg(p, c))
				continue;
			if (c->top)
				p->pend |= c->he->pend_out;
			if (c->top == 2)
				top_event(p, c->he, 0, 0, 1, 1);
			if (unlikely(pp_drcov))
				drcov_end(p);
			p->depth--;
			continue;
		}
		if (c->barrier) {
			mkeof(t);
			return;
		}
		if (c->type == C_FILE) {
			if (p->nfile == 1) {
				mkeof(t);
				return;
			}
			if (unlikely(p->rec.on) && p->depth == p->rec.lev[p->rec.n - 1].depth)
				hc_end(p);
			p->nfile--;
			p->depth--;
			continue;
		}
		if (c->mid)
			menable(p, c->mid);
		if (c->first)
			p->pend |= c->first & 3;
		p->depth--;
	}
}

/* raw_fast's fallback: pops exhausted macro contexts (as raw() does) without raw()'s frame */
VPRIV __attribute__((noinline)) void raw_pop(pp *p, ptok *t)
{
	ctx *c = &p->cx[p->depth - 1];

	while (c->i >= c->n && (c->type == C_PTOK || c->type == C_VTOK) && !c->barrier) {
		if (c->mid)
			menable(p, c->mid);
		if (c->first)
			p->pend |= c->first & 3;
		p->depth--;
		c--;
	}
	if (raw_plain(p, c, t))
		return;
	raw(p, t);
}

/* is the next token '(' ?  (clang's isNextPPTokenLParen: no popping) */
static bool peek_lparen(const pp *p)
{
	for (int d = p->depth - 1; d >= 0; d--) {
		const ctx *c = &p->cx[d];

		if (c->i < c->n)
			return (c->type == C_PTOK ? c->pt[c->i].kind : c->type == C_REPLAY ? c->ht[c->i].kind
							  : c->vt[c->i].kind) == T_LPAREN;
		if (c->type == C_FILE || c->barrier)
			return false;
	}
	return false;
}

/* next_exp() from an identifier t whose entry e is a macro (or disabled) */
VPRIV __attribute__((noinline)) void next_exp_slow(pp *p, ptok *t, uintptr_t e)
{
	for (;;) {
		const pmacro *m;

		if (e & 1) {
			t->flags |= PF_NOEXP;
			return;
		}
		m = (const pmacro *)e;
		if (m->kind == MK_FUNC && !peek_lparen(p))
			return;
		switch (expand(p, m, t)) {
		case 1:
			return;		/* t is the result */
		case 0:
			raw_fast(p, t);
			break;
		default:
			break;		/* t is the next raw token */
		}
		if (t->kind != T_IDENT || (t->flags & PF_NOEXP))
			return;
		e = mread_tok(p, t, 0);
		if (likely(!e))
			return;
	}
}
