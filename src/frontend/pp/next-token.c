/* vcc preprocessor: pp_next, the pull interface that returns one expanded token */
#include "ppimpl.h"

/* the header cache's mode: text for pp_text, tokens for pp_next */
static int hc_off;
int pp_drcov;

/* once: a thread that saw the flag but not hc_mem_max stored without a cap (TSan) */
static void hc_env(void)
{
	const char *m = getenv("VPP_HC_MAX_MB");

	hc_mem_max = m ? (size_t)atol(m) << 20 : (size_t)8 << 30;
	hc_off = getenv("VPP_NOHC") != NULL;
	pp_drcov = getenv("VCC_DRCOV") || getenv("VCC_DRREC") || getenv("VCC_DR");
}

VPRIV void hc_mode(pp *p, int mode)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, hc_env);
	p->hc = !hc_off && !p->o->show_includes && !p->o->dump_defines && !p->o->comments && !p->o->markers && !p->o->imacros;	/* -H: a replay has no nesting to print */
	p->tokmode = mode;
}

static __attribute__((noinline)) void pp_next_slow(pp *p, ptok *t)
{
	if (unlikely(!p->tokmode))
		hc_mode(p, 2);
	p->rep_norec = 0;
	next_tok(p, t);
	p->ntok_out++;
	if (unlikely(p->rec.on) && !p->rep_norec)
		rec_tok(p, t);
}

/* t names a macro (entry e): pp_next's expansion path (not recording) */
static __attribute__((noinline)) void pp_next_mac(pp *p, ptok *t, uintptr_t e)
{
	next_exp_slow(p, t, e);
	p->ntok_out++;
}

void pp_hold_tmp(pp *p, int on) { p->hold_tmp = on != 0; }

void pp_next(pp *p, ptok *t)
{
	ctx *c = &p->cx[p->depth - 1];

	/* a cached header's tokens: straight out, unless a child or __COUNTER__ digits come next */
	if (c->type == C_REPLAY && c->i < c->n) {
		const hent *e = c->he;

		if ((c->si >= e->nseg || e->seg[c->si].at != c->i) && (c->ci >= e->nctr || e->ctr[c->ci].off != c->i)) {
			p->ntok_out++;
			htok_get(t, &c->ht[c->i++], c->pt, c->src, c->loc);
			if (t->kind == T_IDENT)
				t->flags |= PF_NOEXP;
			if (unlikely(p->rec.on) && !c->norec)
				rec_tok(p, t);
			return;
		}
	}
	/* a plain token with nothing owed and no macro: inline, no frame (the rest are tail calls) */
	if (likely(p->tokmode && !p->rec.on && c->i < c->n && !(c->first | p->pend))) {
		uintptr_t m;

		if (c->type == C_PTOK) {
			*t = c->pt[c->i++];
		} else if (c->type == C_VTOK) {
			const vtok *v = &c->vt[c->i++];

			vfill(t, v, c->src, c->offloc ? c->loc + v->off : c->loc);
		} else if (c->type == C_FILE && !((c->vt[c->i].flags & TF_SOL) && c->vt[c->i].kind == T_HASH)) {
			const vtok *v = &c->vt[c->i++];

			if (p->depth == p->nfile && !p->svn && !p->hold_tmp)
				p->tmp.p = p->tmp.base;	/* nothing above file level refers to tmp */
			vfill(t, v, c->src, c->loc + v->off);
		} else {
			pp_next_slow(p, t);
			return;
		}
		if (t->kind == T_IDENT && !(t->flags & PF_NOEXP) && (m = mget(p, t->id))) {
			pp_next_mac(p, t, m);
			return;
		}
		p->ntok_out++;
		return;
	}
	pp_next_slow(p, t);
}
