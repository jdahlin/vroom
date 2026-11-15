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
	p->header_cache_on = !pp_header_cache_disabled && !p->options->show_includes && !p->options->dump_defines && !p->options->comments && !p->options->markers && !p->options->imacros;	/* -H: a replay has no nesting to print */
	p->output_mode = mode;
}

static __attribute__((noinline)) void pp_token_next_slow(pp *p, ptok *t)
{
	if (unlikely(!p->output_mode))
		pp_header_cache_mode_set(p, 2);
	p->token_unrecorded = 0;
	pp_token_next_resetting_temporary(p, t);
	p->tokens_delivered++;
	if (unlikely(p->recording.on) && !p->token_unrecorded)
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
	pp_context *c = &p->contexts[p->context_depth - 1];

	/* a cached header's tokens: straight out, unless a child or __COUNTER__ digits come next */
	if (c->type == PP_CONTEXT_SERVED && c->index < c->count) {
		const pp_header_cache_entry *e = c->entry;

		if ((c->next_segment >= e->segment_count || e->segments[c->next_segment].at != c->index) && (c->next_counter_run >= e->counter_run_count || e->counter_runs[c->next_counter_run].off != c->index)) {
			p->tokens_delivered++;
			pp_header_cache_token_load(t, &c->stored_tokens[c->index++], c->tokens, c->source, c->location);
			if (t->kind == LEX_TOKEN_IDENTIFIER)
				t->flags |= PP_TOKEN_FLAG_NO_EXPAND;
			if (unlikely(p->recording.on) && !c->unrecorded)
				pp_header_cache_token_record(p, t);
			return;
		}
	}
	/* a plain token with nothing owed and no macro: inline, no frame (the rest are tail calls) */
	if (likely(p->output_mode && !p->recording.on && c->index < c->count && !(c->first_flags | p->owed_spacing))) {
		uintptr_t m;

		if (c->type == PP_CONTEXT_TOKENS) {
			*t = c->tokens[c->index++];
		} else if (c->type == PP_CONTEXT_LEXED) {
			const lex_token *v = &c->lexed_tokens[c->index++];

			pp_token_fill_from_lexer(t, v, c->source, c->location_per_token ? c->location + v->off : c->location);
		} else if (c->type == PP_CONTEXT_FILE && !((c->lexed_tokens[c->index].flags & LEX_TOKEN_FLAG_LINE_START) && c->lexed_tokens[c->index].kind == LEX_TOKEN_HASH)) {
			const lex_token *v = &c->lexed_tokens[c->index++];

			if (p->context_depth == p->file_context_count && !p->scratch_token_count && !p->temporary_held)
				p->temporary_region.used = 0;	/* nothing above file level refers to tmp */
			pp_token_fill_from_lexer(t, v, c->source, c->location + v->off);
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
