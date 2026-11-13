/* vcc preprocessor: token contexts: the stack of token sources pp_token_next reads from */
#include "pp-internal.h"

/* ---------------- tokens ---------------- */

_Static_assert(offsetof(lex_token, id) == offsetof(lex_token, len) + 4 && offsetof(ptok, id) == offsetof(ptok, len) + 4 &&
	       offsetof(lex_token, flags) == offsetof(lex_token, kind) + 1 && offsetof(ptok, kind) == offsetof(ptok, loc) + 4 &&
	       offsetof(ptok, flags) == offsetof(ptok, kind) + 1 && offsetof(ptok, ctr) == offsetof(ptok, kind) + 2 &&
	       sizeof(lex_token) == offsetof(lex_token, kind) + 4, "vfill's layout");

static inline void pp_token_eof_make(ptok *t)
{
	memset(t, 0, sizeof(*t));
	t->kind = LEX_TOKEN_EOF;
}

static size_t pp_splice_length(const char *s, size_t i, size_t n)
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

const char *pp_token_spell(const ptok *t, char *buf, size_t *len)
{
	size_t n = 0, l;

	if (!(t->flags & LEX_TOKEN_FLAG_SPLICED)) {
		*len = t->len;
		return t->s;
	}
	for (size_t i = 0; i < t->len;) {
		if ((l = pp_splice_length(t->s, i, t->len))) {
			i += l;
			continue;
		}
		buf[n++] = t->s[i++];
	}
	*len = n;
	return buf;
}

/* clean spelling appended to dst; returns the new end */
VPRIV char *pp_token_spell_to(char *dst, const ptok *t)
{
	size_t n;
	const char *s = pp_token_spell(t, dst, &n);

	if (s != dst)
		memcpy(dst, s, n);
	return dst + n;
}

/* sv[base..svn) to tmp; svn = base */
VPRIV ptok *pp_scratch_tokens_take(pp *p, size_t base, uint32_t *n)
{
	size_t k = p->scratch_token_count - base;
	ptok *r = varena_alloc(&p->temporary_region, (k ? k : 1) * sizeof(ptok));

	memcpy(r, p->scratch_tokens + base, k * sizeof(ptok));
	p->scratch_token_count = base;
	*n = (uint32_t)k;
	return r;
}

VPRIV void pp_context_tokens_push(pp *p, const ptok *t, uint32_t n, uint32_t mid, uint8_t first, int barrier)
{
	pp_context *c = pp_context_push(p);

	c->type = PP_CONTEXT_TOKENS;
	c->tokens = t;
	c->count = n;
	c->macro_to_enable = mid;
	c->first_flags = first;
	c->trailing_space = 0;
	c->barrier = (uint8_t)barrier;
}

/* tokens [b, e) of the current directive line as a barrier context */
VPRIV void pp_context_directive_line_push(pp *p, const pp_context *fc, uint32_t b, uint32_t e)
{
	pp_context *c = pp_context_push(p);

	c->type = PP_CONTEXT_LEXED;
	c->lexed_tokens = fc->lexed_tokens + b;
	c->count = e > b ? e - b : 0;
	c->source = fc->source;
	c->location = fc->location;
	c->location_per_token = 1;
	c->barrier = 1;
}

/* pop everything down to and including the barrier context at depth d */
VPRIV void pp_context_pop_to(pp *p, int d)
{
	while (p->context_depth > d) {
		pp_context *c = &p->contexts[p->context_depth - 1];

		if (c->type == PP_CONTEXT_FILE) {
			p->file_context_count--;
		} else if (c->macro_to_enable) {
			pp_macro_table_enable(p, c->macro_to_enable);
		}
		p->context_depth--;
	}
}

/* a top-level region event (pp_top_events) at the current delivered position */
VPRIV void pp_top_event_record(pp *p, const void *e, uint32_t base, unsigned ctr, uint8_t end, uint8_t hit)
{
	if (p->top_events_full)
		return;
	if (!p->top_events)
		p->top_events = varena_alloc(&p->permanent_region, 1024 * sizeof(pp_topev));
	if (p->top_event_count == 1024) {
		p->top_events_full = 1;
		return;
	}
	p->top_events[p->top_event_count++] = (pp_topev){ e, p->tokens_delivered, base, ctr, end, hit };
}

/* the next token, no macro expansion; runs directives in file contexts */
VPRIV __attribute__((noinline)) void pp_token_raw_next(pp *p, ptok *t)
{
	for (;;) {
		pp_context *c = &p->contexts[p->context_depth - 1];

		if (likely(c->index < c->count)) {
			if (c->type == PP_CONTEXT_TOKENS) {
				*t = c->tokens[c->index++];
			} else if (c->type == PP_CONTEXT_LEXED) {
				const lex_token *v = &c->lexed_tokens[c->index++];

				pp_token_fill_from_lexer(t, v, c->source, c->location_per_token ? c->location + v->off : c->location);
			} else if (c->type == PP_CONTEXT_SERVED) {
				if (pp_header_cache_serve_child(p, c))
					continue;
				pp_header_cache_serve_token(p, c, t);
			} else {
				const lex_token *v = &c->lexed_tokens[c->index];

				if (unlikely((v->flags & LEX_TOKEN_FLAG_LINE_START) && v->kind == LEX_TOKEN_HASH)) {
					if (pp_directive_run(p, c, t))
						return;
					continue;
				}
				c->index++;
				pp_token_fill_from_lexer(t, v, c->source, c->location + v->off);
			}
			if (c->first_flags) {
				t->flags = (t->flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | (c->first_flags & 3);
				c->first_flags = 0;
			}
			if (p->owed_spacing) {
				t->flags |= p->owed_spacing;
				p->owed_spacing = 0;
			}
			return;
		}
		if (c->type == PP_CONTEXT_SERVED) {
			if (pp_header_cache_serve_child(p, c))
				continue;
			if (c->top_level)
				p->owed_spacing |= c->entry->owed_spacing_at_end;
			if (c->top_level == 2)
				pp_top_event_record(p, c->entry, 0, 0, 1, 1);
			if (unlikely(pp_coverage_enabled))
				pp_coverage_hit_end(p);
			p->context_depth--;
			continue;
		}
		if (c->barrier) {
			pp_token_eof_make(t);
			return;
		}
		if (c->type == PP_CONTEXT_FILE) {
			if (unlikely(p->output_text_mode > 1))
				pp_comments_flush(p, c, c->source + c->lex_file->size);
			if (p->file_context_count == 1) {
				pp_token_eof_make(t);
				return;
			}
			if (unlikely(p->recording.on) && p->context_depth == p->recording.levels[p->recording.level_count - 1].depth)
				pp_header_cache_region_end(p);
			if (unlikely(p->diagnostic_transition_count))
				diag_pragma_file_end(p, c->file_index);
			p->file_context_count--;
			p->context_depth--;
			if (unlikely(p->output_text_mode))
				pp_output_file_change(p, pp_file_context_current(p), 0);
			continue;
		}
		if (c->macro_to_enable)
			pp_macro_table_enable(p, c->macro_to_enable);
		if (c->first_flags)
			p->owed_spacing |= c->first_flags & 3;
		if (c->type == PP_CONTEXT_TOKENS && c->trailing_space)
			p->owed_spacing |= LEX_TOKEN_FLAG_SPACE_BEFORE;
		p->context_depth--;
		if (unlikely(p->line_start_name_active) && p->context_depth <= p->line_start_context_depth && !p->collecting_arguments)
			p->line_start_name_active = 0;	/* the line's first expansion printed nothing */
	}
}

/* pp_token_raw_fast_next's fallback: pops exhausted macro contexts (as pp_token_raw_next() does) without pp_token_raw_next()'s frame */
VPRIV __attribute__((noinline)) void pp_token_raw_pop(pp *p, ptok *t)
{
	pp_context *c = &p->contexts[p->context_depth - 1];

	while (c->index >= c->count && (c->type == PP_CONTEXT_TOKENS || c->type == PP_CONTEXT_LEXED) && !c->barrier) {
		if (c->macro_to_enable)
			pp_macro_table_enable(p, c->macro_to_enable);
		if (c->first_flags)
			p->owed_spacing |= c->first_flags & 3;
		if (c->type == PP_CONTEXT_TOKENS && c->trailing_space)
			p->owed_spacing |= LEX_TOKEN_FLAG_SPACE_BEFORE;
		p->context_depth--;
		if (unlikely(p->line_start_name_active) && p->context_depth <= p->line_start_context_depth && !p->collecting_arguments)
			p->line_start_name_active = 0;	/* the line's first expansion printed nothing */
		c--;
	}
	if (pp_token_raw_plain_next(p, c, t))
		return;
	pp_token_raw_next(p, t);
}

/* is the next token '(' ?  (clang's isNextPPTokenLParen: no popping) */
static bool pp_next_is_left_paren(const pp *p)
{
	for (int d = p->context_depth - 1; d >= 0; d--) {
		const pp_context *c = &p->contexts[d];

		if (c->index < c->count)
			return (c->type == PP_CONTEXT_TOKENS ? c->tokens[c->index].kind : c->type == PP_CONTEXT_SERVED ? c->stored_tokens[c->index].kind
							  : c->lexed_tokens[c->index].kind) == LEX_TOKEN_LEFT_PAREN;
		if (c->type == PP_CONTEXT_FILE || c->barrier)
			return false;
	}
	return false;
}

/* pp_token_expanded_next() from an identifier t whose entry e is a macro (or disabled) */
VPRIV __attribute__((noinline)) void pp_token_expanded_next_slow(pp *p, ptok *t, uintptr_t e)
{
	for (;;) {
		const pp_macro *m;

		if (e & 1) {
			t->flags |= PP_TOKEN_FLAG_NO_EXPAND;
			return;
		}
		m = (const pp_macro *)e;
		if (m->kind == PP_MACRO_FUNCTION_LIKE && !pp_next_is_left_paren(p))
			return;
		switch (pp_macro_expand(p, m, t)) {
		case 1:
			return;		/* t is the result */
		case 0:
			pp_token_raw_fast_next(p, t);
			break;
		default:
			break;		/* t is the next raw token */
		}
		if (t->kind != LEX_TOKEN_IDENTIFIER || (t->flags & PP_TOKEN_FLAG_NO_EXPAND))
			return;
		e = pp_macro_read_token(p, t, 0);
		if (likely(!e))
			return;
	}
}

/*
 * assembler-with-cpp: '$' is no identifier character (clang), so in "$APIC_ID"
 * the macro APIC_ID expands: t becomes the '$' and the name is read next.
 * (vcc's lexer is shared with C, where '$' is one: the split is made here.)
 */
VPRIV __attribute__((noinline, cold)) void pp_dollar_split(pp *p, ptok *t)
{
	uint32_t id;

	if (t->len < 2 || t->s[0] != '$' || (t->flags & LEX_TOKEN_FLAG_SPLICED) || t->s[1] == '$' || (t->s[1] >= '0' && t->s[1] <= '9'))
		return;
	id = vintern_find(t->s + 1, t->len - 1, vintern_hash(t->s + 1, t->len - 1));
	if (!id || !pp_macro_table_get(p, id))
		return;
	{
		ptok *n = varena_alloc(&p->temporary_region, sizeof(*n));

		*n = *t;
		n->s = t->s + 1, n->len = t->len - 1, n->id = id, n->loc = t->loc + 1;
		n->flags &= (uint8_t)~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE);
		pp_context_tokens_push(p, n, 1, 0, 0, 0);
		t->len = 1, t->id = 0, t->kind = LEX_TOKEN_UNKNOWN;
	}
}
