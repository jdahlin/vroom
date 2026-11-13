/* vcc preprocessor: pp_token_next, the pull interface that returns one expanded token */
#include "pp-internal.h"

/* the header cache's mode: text for pp_text_render, tokens for pp_token_next */
static int pp_header_cache_disabled;
int pp_coverage_enabled;

/* once: a thread that saw the flag but not pp_header_cache_memory_limit stored without a cap (TSan) */
static void pp_header_cache_environment_read(void)
{
	const char *m = getenv("VPP_HC_MAX_MB");

	pp_header_cache_memory_limit = m ? (size_t)atol(m) << 20 : (size_t)8 << 30;
	pp_header_cache_disabled = getenv("VPP_NOHC") != NULL;
	pp_coverage_enabled = getenv("VCC_DRCOV") || getenv("VCC_DRREC") || getenv("VCC_DR");
}

VPRIV void pp_header_cache_mode_set(pp *p, int mode)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, pp_header_cache_environment_read);
	p->hc = !pp_header_cache_disabled && !p->o->show_includes && !p->o->dump_defines && !p->o->comments && !p->o->markers && !p->o->imacros;	/* -H: a replay has no nesting to print */
	p->output_mode = mode;
}

static __attribute__((noinline)) void pp_token_next_slow(pp *p, ptok *t)
{
	if (unlikely(!p->output_mode))
		pp_header_cache_mode_set(p, 2);
	p->token_unrecorded = 0;
	pp_token_next_resetting_temporary(p, t);
	p->tokens_delivered++;
	if (unlikely(p->rec.on) && !p->token_unrecorded)
		pp_header_cache_token_record(p, t);
}

/* t names a macro (entry e): pp_token_next's expansion path (not recording) */
static __attribute__((noinline)) void pp_token_next_macro(pp *p, ptok *t, uintptr_t e)
{
	pp_token_expanded_next_slow(p, t, e);
	p->tokens_delivered++;
}

void pp_temporary_hold(pp *p, int on) { p->temporary_held = on != 0; }

void pp_token_next(pp *p, ptok *t)
{
	pp_context *c = &p->cx[p->depth - 1];

	/* a cached header's tokens: straight out, unless a child or __COUNTER__ digits come next */
	if (c->type == PP_CONTEXT_SERVED && c->i < c->n) {
		const pp_header_cache_entry *e = c->he;

		if ((c->si >= e->nseg || e->seg[c->si].at != c->i) && (c->ci >= e->nctr || e->ctr[c->ci].off != c->i)) {
			p->tokens_delivered++;
			pp_header_cache_token_load(t, &c->ht[c->i++], c->pt, c->src, c->loc);
			if (t->kind == LEX_TOKEN_IDENTIFIER)
				t->flags |= PP_TOKEN_FLAG_NO_EXPAND;
			if (unlikely(p->rec.on) && !c->norec)
				pp_header_cache_token_record(p, t);
			return;
		}
	}
	/* a plain token with nothing owed and no macro: inline, no frame (the rest are tail calls) */
	if (likely(p->output_mode && !p->rec.on && c->i < c->n && !(c->first | p->pend))) {
		uintptr_t m;

		if (c->type == PP_CONTEXT_TOKENS) {
			*t = c->pt[c->i++];
		} else if (c->type == PP_CONTEXT_LEXED) {
			const lex_token *v = &c->vt[c->i++];

			pp_token_fill_from_lexer(t, v, c->src, c->offloc ? c->loc + v->off : c->loc);
		} else if (c->type == PP_CONTEXT_FILE && !((c->vt[c->i].flags & LEX_TOKEN_FLAG_LINE_START) && c->vt[c->i].kind == LEX_TOKEN_HASH)) {
			const lex_token *v = &c->vt[c->i++];

			if (p->depth == p->nfile && !p->scratch_token_count && !p->temporary_held)
				p->tmp.used = 0;	/* nothing above file level refers to tmp */
			pp_token_fill_from_lexer(t, v, c->src, c->loc + v->off);
		} else {
			pp_token_next_slow(p, t);
			return;
		}
		if (t->kind == LEX_TOKEN_IDENTIFIER && !(t->flags & PP_TOKEN_FLAG_NO_EXPAND) && (m = pp_macro_table_get(p, t->id))) {
			pp_token_next_macro(p, t, m);
			return;
		}
		p->tokens_delivered++;
		return;
	}
	pp_token_next_slow(p, t);
}
