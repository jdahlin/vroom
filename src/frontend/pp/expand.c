/* vcc preprocessor: macro expansion: arguments, stringizing, token pasting, substitution */
#include "pp-internal.h"
#include <ctype.h>

/* ---------------- arguments ---------------- */

typedef struct pp_macro_arguments {
	const ptok *tok;
	uint32_t *st, *ln;	/* per parameter: start, length in tok */
	const ptok **pre;	/* pre-expanded, lazily */
	uint32_t *preexpanded_lengths;
	uint8_t *preexpanded_state;		/* 0: not computed, 1: done */
	size_t base;		/* tok is sv + base */
	bool variadic_elided;		/* the variadic argument was left out (clang's isVarargsElidedUse) */
} pp_macro_arguments;

/* "macro 'X' defined here": the #define's name, found from the body's file (errors only) */
VPRIV void pp_macro_note_at(pp *p, const pp_macro *m, uint32_t before, const char *msg)
{
	uint32_t loc = 0, len = 0;
	const char *src = NULL;

	/* the last "#define NAME" seen (a redefinition replaces the earlier one) */
	for (uint32_t i = 0; i < p->nfe; i++) {
		const lex_file *f = p->fe[i].f;
		const lex_token *q;

		if (!f || !f->tok)
			continue;
		q = f->tok;
		for (uint32_t k = 2; k < f->ntok; k++)
			if (q[k].kind == LEX_TOKEN_IDENTIFIER && q[k].id == m->name && q[k - 1].id == KW_define &&
			    q[k - 2].kind == LEX_TOKEN_HASH && (q[k - 2].flags & LEX_TOKEN_FLAG_LINE_START) && p->fe[i].base + q[k].off < before)
				loc = p->fe[i].base + q[k].off, len = q[k].len, src = f->src + q[k].off;
	}
	if (src) {
		pp_dinfo d = { "note", loc, NULL, 0, NULL, 0, NULL, 0 };

		pp_diagx(p, &d, msg, (int)len, src);
	}
}

static void pp_macro_note(pp *p, const pp_macro *m)
{
	pp_macro_note_at(p, m, UINT32_MAX, "macro '%.*s' defined here");
}

static bool pp_macro_arguments_collect_inner(pp *p, const pp_macro *m, const ptok *name, pp_macro_arguments *a, ptok *rp)
{
	uint32_t np = m->nparam, slots = np ? np : 1, k = 0, depth = 0, end;
	size_t base = p->scratch_token_count;
	bool va = m->flags & PP_MACRO_FLAG_VARIADIC;
	const ptok *tok;
	ptok t;

	a->variadic_elided = false;
	/* pre, then st, ln, preln, then prest: one allocation */
	{
		char *b = varena_alloc(&p->tmp, slots * (sizeof(*a->pre) + 3 * sizeof(uint32_t) + 1));

		a->pre = (const ptok **)b;
		a->st = (uint32_t *)(b + slots * sizeof(*a->pre));
		a->ln = a->st + slots;
		a->preexpanded_lengths = a->ln + slots;
		a->preexpanded_state = (uint8_t *)(a->preexpanded_lengths + slots);
	}
	pp_token_raw_next(p, &t);		/* '(' */
	a->st[0] = 0;
	/*
	 * The whole invocation in the top expansion context (nested macros):
	 * the arguments are slices of its tokens, which outlive the expansion
	 * (tmp) and are not written; nothing is copied.
	 */
	{
		pp_context *c = &p->cx[p->depth - 1];

		if (c->type == PP_CONTEXT_TOKENS && !(c->first | p->pend)) {
			const ptok *pt = c->pt + c->i;
			uint32_t e = c->n - c->i;

			for (uint32_t i = 0; i < e; i++) {
				uint8_t kd = pt[i].kind;

				if (kd == LEX_TOKEN_LEFT_PAREN) {
					depth++;
				} else if (kd == LEX_TOKEN_RIGHT_PAREN) {
					if (depth) {
						depth--;
						continue;
					}
					c->i += i + 1;
					*rp = pt[i];
					tok = pt;
					end = i;
					goto done;
				} else if (kd == LEX_TOKEN_COMMA && !depth && !(va && k + 1 >= np)) {
					if (k + 1 >= slots) {
						k++;	/* too many: counted, reported below */
						continue;
					}
					a->ln[k] = i - a->st[k];
					a->st[++k] = i + 1;
				}
			}
			k = depth = 0;	/* not all there: collect into sv */
		}
	}
	for (;;) {
		pp_context *c = &p->cx[p->depth - 1];

		/* runs of plain tokens straight into sv */
		if (likely(c->i < c->n && !(c->first | p->pend)) && c->type != PP_CONTEXT_SERVED) {
			bool stop_comma = !(va && k + 1 >= np);
			uint32_t i = c->i, e = c->n;

			if (c->type == PP_CONTEXT_TOKENS) {
				const ptok *pt = c->pt;
				ptok *w = p->sv + p->scratch_token_count;

				/* (no room for the rest of the context: token by token below) */
				if (unlikely(p->scratch_token_count + (e - i) > PP_SCRATCH_TOKENS_MAX))
					e = i;
				for (; i < e; i++) {
					uint8_t kd = pt[i].kind;

					if (kd == LEX_TOKEN_LEFT_PAREN) {
						depth++;
					} else if (kd == LEX_TOKEN_RIGHT_PAREN) {
						if (!depth)
							break;
						depth--;
					} else if (kd == LEX_TOKEN_COMMA && !depth && stop_comma) {
						break;
					}
					*w++ = pt[i];
				}
				p->scratch_token_count = (size_t)(w - p->sv);
				e = c->n;
			} else {
				const lex_token *vt = c->vt;
				bool file = c->type == PP_CONTEXT_FILE;
				uint32_t loc = c->loc, offloc = file || c->offloc;

				for (; i < e; i++) {
					const lex_token *v = &vt[i];
					uint8_t kd = v->kind;

					if (kd == LEX_TOKEN_LEFT_PAREN) {
						depth++;
					} else if (kd == LEX_TOKEN_RIGHT_PAREN) {
						if (!depth)
							break;
						depth--;
					} else if (kd == LEX_TOKEN_COMMA && !depth && stop_comma) {
						break;
					} else if (file && (v->flags & LEX_TOKEN_FLAG_LINE_START) && kd == LEX_TOKEN_HASH) {
						break;
					}
					if (unlikely(p->scratch_token_count == PP_SCRATCH_TOKENS_MAX)) {
						PP_FATAL(p, 0, "macro expansion too large");
					}
					pp_token_fill_from_lexer(&p->sv[p->scratch_token_count++], v, c->src, offloc ? loc + v->off : loc);
				}
			}
			c->i = i;
			if (i == e)
				continue;
		}
		pp_token_raw_fast_next(p, &t);
		if (unlikely(t.kind == LEX_TOKEN_EOF)) {
			PP_ERROR(p, name->loc, "unterminated function-like macro invocation");
			p->scratch_token_count = base;
			*rp = t;	/* the end of the file: clang drops the invocation, its name too */
			return false;
		}
		if (t.kind == LEX_TOKEN_LEFT_PAREN) {
			depth++;
		} else if (t.kind == LEX_TOKEN_RIGHT_PAREN) {
			if (!depth)
				break;
			depth--;
		} else if (t.kind == LEX_TOKEN_COMMA && !depth && !(va && k + 1 >= np)) {
			if (k + 1 >= slots) {
				k++;
				continue;	/* too many: counted, reported below */
			}
			a->ln[k] = (uint32_t)(p->scratch_token_count - base) - a->st[k];
			a->st[++k] = (uint32_t)(p->scratch_token_count - base);
			continue;
		} else if (unlikely(t.kind == PP_TOKEN_KIND_DIRECTIVE_LINE)) {
			PP_WARN(p, t.loc, "directive in macro arguments ignored");
			continue;
		}
		pp_scratch_token_push(p, &t);
	}
	*rp = t;
	/* the arguments stay in sv[base, svn) until pp_macro_expand() is done with them */
	tok = p->sv + base;
	end = (uint32_t)(p->scratch_token_count - base);
done:
	if (k >= slots) {
		PP_ERROR(p, name->loc, "too many arguments provided to function-like macro invocation");
		p->scratch_token_count = base;
		return false;
	}
	a->ln[k] = end - a->st[k];
	if (k + 1 < np) {
		if (va && k + 2 == np) {
			/* GNU: the variadic argument left out entirely */
			a->st[k + 1] = end;
			a->ln[k + 1] = 0;
			a->variadic_elided = true;
		} else {
			PP_ERROR(p, t.loc, "too few arguments provided to function-like macro invocation");
			pp_macro_note(p, m);
			p->scratch_token_count = base;
			return false;
		}
	} else if (np == 1 && va && !a->ln[0]) {
		a->variadic_elided = true;	/* F() for F(...): no arguments at all */
	} else if (np == 0 && a->ln[0]) {
		PP_ERROR(p, name->loc, "too many arguments provided to function-like macro invocation");
		p->scratch_token_count = base;
		return false;
	}
	if (unlikely(p->output_text_mode))
		for (ptok *q = p->sv + base, *qe = q + end; q < qe; q++) {	/* clang -E prints an argument on its invocation's line */
			if (q->flags & LEX_TOKEN_FLAG_LINE_START)	/* the line break is no space, the indentation is */
				q->flags = (q->flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | (pp_token_column(p, q->loc) > 1 ? LEX_TOKEN_FLAG_SPACE_BEFORE : 0);
			if (unlikely(p->output_text_mode > 1) && (q->flags & LEX_TOKEN_FLAG_SPACE_BEFORE) && !(q->flags & PP_TOKEN_FLAG_FROM_MACRO) && q->s[-1] == '/')
				q->flags &= ~LEX_TOKEN_FLAG_SPACE_BEFORE;	/* -C: right after a comment (which is dropped) */
		}
	a->tok = tok;
	a->base = base;
	memset(a->preexpanded_state, 0, slots);
	return true;
}

/* collect failed: the name stays, as clang's; at the end of the file it is dropped */
static __attribute__((noinline, cold)) int pp_macro_arguments_collect_fail(ptok *t, const ptok *rp)
{
	if (rp->kind == LEX_TOKEN_EOF)
		*t = *rp;
	return 1;
}

static bool pp_macro_arguments_collect(pp *p, const pp_macro *m, const ptok *name, pp_macro_arguments *a, ptok *rp)
{
	bool r;

	if (!p->collecting)
		p->coll_loc = name->loc, p->collecting_name = m->name;
	p->collecting++;
	r = pp_macro_arguments_collect_inner(p, m, name, a, rp);
	p->collecting--;
	return r;
}

/* clang's MacroArgs::ArgNeedsPreexpansion: any identifier with a macro */
static bool pp_macro_argument_needs_preexpansion(pp *p, const ptok *t, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (t[i].kind == LEX_TOKEN_IDENTIFIER && pp_macro_read_token(p, &t[i], 1))
			return true;
	return false;
}

/*
 * keep: the result stays in sv[base, svn) (it is only read, and only until
 * pp_macro_expand() resets svn); else it is copied to tmp (pp_va_opt_expand: sv is in use above)
 */
static __attribute__((noinline)) void pp_macro_argument_preexpand_run(pp *p, pp_macro_arguments *a, uint32_t k, bool keep,
						       const ptok *at, uint32_t n)
{
	uint8_t pend = p->pend;
	int d = p->depth;
	size_t base = p->scratch_token_count;
	ptok t;

	if (vstack_guard_low())
		PP_FATAL(p, at->loc, "macro arguments nested too deeply");
	pp_context_tokens_push(p, at, n, 0, 0, 1);
	p->pend = 0;
	p->argument_preexpansion++;
	for (;;) {
		pp_context *c = &p->cx[p->depth - 1];

		/* runs of tokens that are not macros: as pp_token_expanded_next() returns them, straight into sv */
		if (c->type == PP_CONTEXT_TOKENS && c->i < c->n && !(c->first | p->pend) && !p->rec.on &&
		    likely(p->scratch_token_count + (c->n - c->i) <= PP_SCRATCH_TOKENS_MAX)) {
			const ptok *pt = c->pt;
			ptok *w = p->sv + p->scratch_token_count;
			uint32_t i = c->i, e = c->n;

			for (; i < e; i++) {
				uintptr_t me;

				*w = pt[i];
				if (pt[i].kind == LEX_TOKEN_IDENTIFIER && !(pt[i].flags & PP_TOKEN_FLAG_NO_EXPAND) && (me = pp_macro_table_get(p, pt[i].id))) {
					if (!(me & 1))
						break;	/* a macro: pp_token_expanded_next() */
					w->flags |= PP_TOKEN_FLAG_NO_EXPAND;
				}
				w++;
			}
			p->scratch_token_count = (size_t)(w - p->sv);
			c->i = i;
		}
		pp_token_expanded_next(p, &t);
		if (t.kind == LEX_TOKEN_EOF)
			break;
		pp_scratch_token_push(p, &t);
	}
	p->argument_preexpansion--;
	pp_context_pop_to(p, d);
	p->pend = pend;
	if (keep) {
		a->pre[k] = p->sv + base;
		a->preexpanded_lengths[k] = (uint32_t)(p->scratch_token_count - base);
	} else {
		a->pre[k] = pp_scratch_tokens_take(p, base, &a->preexpanded_lengths[k]);
	}
}

/* most arguments need no pre-expansion: that check inline, the rest in pp_macro_argument_preexpand_run */
static inline void pp_macro_argument_preexpand(pp *p, pp_macro_arguments *a, uint32_t k, bool keep)
{
	const ptok *at = a->tok + a->st[k];
	uint32_t n = a->ln[k];

	a->preexpanded_state[k] = 1;
	if (!pp_macro_argument_needs_preexpansion(p, at, n)) {
		a->pre[k] = at;
		a->preexpanded_lengths[k] = n;
		return;
	}
	pp_macro_argument_preexpand_run(p, a, k, keep, at, n);
}

VPRIV bool pp_kind_is_string_literal(int k)
{
	return k >= LEX_TOKEN_CHARACTER && k <= LEX_TOKEN_UTF32_STRING;
}

/* "#x" (clang's MacroArgs::StringifyArgument) */
static void pp_macro_argument_stringify(pp *p, const ptok *t, uint32_t n, ptok *out)
{
	size_t cap = 3;
	char *b, *w;

	for (uint32_t i = 0; i < n; i++)
		cap += 2 * t[i].len + 1;
	/* __COUNTER__ digit runs, in order: (run, its offset in the result) */
	uint16_t runs[64];
	uint32_t roff[64], nr = 0;

	b = w = varena_alloc(&p->tmp, cap + 16);
	*w++ = '"';
	for (uint32_t i = 0; i < n; i++) {
		uint16_t rc = unlikely(t[i].ctr) && p->rec.on ? t[i].ctr : 0;

		if (i && (t[i].flags & (LEX_TOKEN_FLAG_SPACE_BEFORE | LEX_TOKEN_FLAG_LINE_START)))
			*w++ = ' ';
		if (pp_kind_is_string_literal(t[i].kind)) {
			char tmp[t[i].len + 1];
			size_t l;
			const char *s = pp_token_spell(&t[i], tmp, &l);

			for (size_t j = 0; j < l; j++) {
				/* digits are never escaped: a run starts where its first digit lands */
				while (rc && p->rec.cp[rc].off == j) {
					if (nr < 64) {
						runs[nr] = rc;
						roff[nr++] = (uint32_t)(w - b);
					} else {
						PP_HEADER_CACHE_REGION_REJECT(p);
					}
					rc = p->rec.cp[rc].next;
				}
				if (s[j] == '\\' || s[j] == '"')
					*w++ = '\\';
				if (s[j] == '\n') {
					*w++ = '\\';
					*w++ = 'n';
					continue;
				}
				*w++ = s[j];
			}
			if (rc)
				PP_HEADER_CACHE_REGION_REJECT(p);
		} else {
			for (; rc; rc = p->rec.cp[rc].next) {
				if (nr < 64) {
					runs[nr] = rc;
					roff[nr++] = (uint32_t)(w - b) + p->rec.cp[rc].off;
				} else {
					PP_HEADER_CACHE_REGION_REJECT(p);
				}
			}
			w = pp_token_spell_to_padded(w, &t[i]);	/* (cap has 16 to spare) */
		}
	}
	/* a trailing unescaped backslash would escape the quote: drop it */
	if (w - b >= 2 && w[-1] == '\\') {
		size_t bs = 0;

		for (char *q = w - 1; q > b && *q == '\\'; q--)
			bs++;
		if (bs & 1)
			w--;
	}
	*w++ = '"';
	memset(w, 0, 16);
	memset(out, 0, sizeof(*out));
	out->s = b;
	out->len = (uint32_t)(w - b);
	out->kind = LEX_TOKEN_STRING;
	out->loc = n ? t[0].loc : 0;
	for (uint32_t k = nr; k-- > 0;) {
		const pp_header_cache_counter_provenance c = p->rec.cp[runs[k]];

		out->ctr = pp_header_cache_counter_provenance_new(p, c.val, roff[k], c.len, out->ctr);
	}
}

/* ---------------- token pasting ---------------- */

static const struct { const char *s; uint8_t k; } pp_punctuator_table[] = {
	{ "[", LEX_TOKEN_LEFT_SQUARE }, { "]", LEX_TOKEN_RIGHT_SQUARE }, { "(", LEX_TOKEN_LEFT_PAREN }, { ")", LEX_TOKEN_RIGHT_PAREN },
	{ "{", LEX_TOKEN_LEFT_BRACE }, { "}", LEX_TOKEN_RIGHT_BRACE }, { ".", LEX_TOKEN_PERIOD }, { "...", LEX_TOKEN_ELLIPSIS },
	{ "&", LEX_TOKEN_AMPERSAND }, { "&&", LEX_TOKEN_AMPERSAND_AMPERSAND }, { "&=", LEX_TOKEN_AMPERSAND_EQUAL }, { "*", LEX_TOKEN_STAR },
	{ "*=", LEX_TOKEN_STAR_EQUAL }, { "+", LEX_TOKEN_PLUS }, { "++", LEX_TOKEN_PLUS_PLUS }, { "+=", LEX_TOKEN_PLUS_EQUAL },
	{ "-", LEX_TOKEN_MINUS }, { "->", LEX_TOKEN_ARROW }, { "--", LEX_TOKEN_MINUS_MINUS }, { "-=", LEX_TOKEN_MINUS_EQUAL },
	{ "~", LEX_TOKEN_TILDE }, { "!", LEX_TOKEN_EXCLAMATION }, { "!=", LEX_TOKEN_EXCLAMATION_EQUAL }, { "/", LEX_TOKEN_SLASH },
	{ "/=", LEX_TOKEN_SLASH_EQUAL }, { "%", LEX_TOKEN_PERCENT }, { "%=", LEX_TOKEN_PERCENT_EQUAL }, { "<", LEX_TOKEN_LESS },
	{ "<<", LEX_TOKEN_LESS_LESS }, { "<=", LEX_TOKEN_LESS_EQUAL }, { "<<=", LEX_TOKEN_LESS_LESS_EQUAL }, { ">", LEX_TOKEN_GREATER },
	{ ">>", LEX_TOKEN_GREATER_GREATER }, { ">=", LEX_TOKEN_GREATER_EQUAL }, { ">>=", LEX_TOKEN_GREATER_GREATER_EQUAL },
	{ "^", LEX_TOKEN_CARET }, { "^=", LEX_TOKEN_CARET_EQUAL }, { "|", LEX_TOKEN_PIPE }, { "||", LEX_TOKEN_PIPE_PIPE },
	{ "|=", LEX_TOKEN_PIPE_EQUAL }, { "?", LEX_TOKEN_QUESTION }, { ":", LEX_TOKEN_COLON }, { ";", LEX_TOKEN_SEMICOLON },
	{ "=", LEX_TOKEN_EQUAL }, { "==", LEX_TOKEN_EQUAL_EQUAL }, { ",", LEX_TOKEN_COMMA }, { "#", LEX_TOKEN_HASH },
	{ "##", LEX_TOKEN_HASH_HASH }, { "<:", LEX_TOKEN_LEFT_SQUARE }, { ":>", LEX_TOKEN_RIGHT_SQUARE }, { "<%", LEX_TOKEN_LEFT_BRACE },
	{ "%>", LEX_TOKEN_RIGHT_BRACE }, { "%:", LEX_TOKEN_HASH }, { "%:%:", LEX_TOKEN_HASH_HASH },
};

#define PP_IDENTIFIER_CHARACTER_ROW(b) [b] = 1, [b + 1] = 1, [b + 2] = 1, [b + 3] = 1, [b + 4] = 1, [b + 5] = 1, [b + 6] = 1, [b + 7] = 1
const uint8_t pp_identifier_character_table[256] = {	/* [a-zA-Z0-9_$] and >= 0x80 */
	['0'] = 1, ['1'] = 1, ['2'] = 1, ['3'] = 1, ['4'] = 1, ['5'] = 1, ['6'] = 1, ['7'] = 1, ['8'] = 1, ['9'] = 1,
	PP_IDENTIFIER_CHARACTER_ROW('A'), PP_IDENTIFIER_CHARACTER_ROW('I'), PP_IDENTIFIER_CHARACTER_ROW('Q'), ['Y'] = 1, ['Z'] = 1,
	PP_IDENTIFIER_CHARACTER_ROW('a'), PP_IDENTIFIER_CHARACTER_ROW('i'), PP_IDENTIFIER_CHARACTER_ROW('q'), ['y'] = 1, ['z'] = 1,
	['_'] = 1, ['$'] = 1,
	PP_IDENTIFIER_CHARACTER_ROW(0x80), PP_IDENTIFIER_CHARACTER_ROW(0x88), PP_IDENTIFIER_CHARACTER_ROW(0x90), PP_IDENTIFIER_CHARACTER_ROW(0x98), PP_IDENTIFIER_CHARACTER_ROW(0xa0), PP_IDENTIFIER_CHARACTER_ROW(0xa8),
	PP_IDENTIFIER_CHARACTER_ROW(0xb0), PP_IDENTIFIER_CHARACTER_ROW(0xb8), PP_IDENTIFIER_CHARACTER_ROW(0xc0), PP_IDENTIFIER_CHARACTER_ROW(0xc8), PP_IDENTIFIER_CHARACTER_ROW(0xd0), PP_IDENTIFIER_CHARACTER_ROW(0xd8),
	PP_IDENTIFIER_CHARACTER_ROW(0xe0), PP_IDENTIFIER_CHARACTER_ROW(0xe8), PP_IDENTIFIER_CHARACTER_ROW(0xf0), PP_IDENTIFIER_CHARACTER_ROW(0xf8),
};
#undef PP_IDENTIFIER_CHARACTER_ROW

/* a UCN (\\uXXXX, \\UXXXXXXXX) at s[i]: its length, else 0 */
static __attribute__((noinline, cold)) size_t pp_ucn_length(const char *s, size_t n, size_t i)
{
	size_t d = i + 1 < n && s[i] == '\\' ? (s[i + 1] == 'u' ? 4 : s[i + 1] == 'U' ? 8 : 0) : 0;

	if (!d || i + 2 + d > n)
		return 0;
	for (size_t k = i + 2; k < i + 2 + d; k++)
		if (!isxdigit((unsigned char)s[k]))
			return 0;
	return 2 + d;
}

/* the kind of the single token spelled s[0..n), or -1 */
static int pp_paste_result_classify(const char *s, size_t n, int rkind)
{
	size_t i;

	if (!n)
		return -1;
	if ((pp_character_is_identifier((unsigned char)s[0]) && !(s[0] >= '0' && s[0] <= '9')) || (s[0] == '\\' && pp_ucn_length(s, n, 0))) {
		for (i = 0; i < n;) {
			size_t u;

			if (pp_character_is_identifier((unsigned char)s[i]))
				i++;
			else if (s[i] == '\\' && (u = pp_ucn_length(s, n, i)))
				i += u;
			else
				break;
		}
		if (i == n)
			return LEX_TOKEN_IDENTIFIER;
		/* encoding prefix + literal */
		if (pp_kind_is_string_literal(rkind)) {
			size_t pl = i;

			if (s[pl] == '"' || s[pl] == '\'') {
				bool ch = s[pl] == '\'';

				if (pl == 1 && s[0] == 'L') return ch ? LEX_TOKEN_WIDE_CHARACTER : LEX_TOKEN_WIDE_STRING;
				if (pl == 1 && s[0] == 'u') return ch ? LEX_TOKEN_UTF16_CHARACTER : LEX_TOKEN_UTF16_STRING;
				if (pl == 1 && s[0] == 'U') return ch ? LEX_TOKEN_UTF32_CHARACTER : LEX_TOKEN_UTF32_STRING;
				if (pl == 2 && s[0] == 'u' && s[1] == '8') return ch ? LEX_TOKEN_UTF8_CHARACTER : LEX_TOKEN_UTF8_STRING;
			}
		}
		return -1;
	}
	if ((s[0] >= '0' && s[0] <= '9') || (s[0] == '.' && n > 1 && s[1] >= '0' && s[1] <= '9')) {
		for (i = 1; i < n; i++) {
			unsigned char c = (unsigned char)s[i];

			if ((c == '+' || c == '-') && strchr("eEpP", s[i - 1]))
				continue;
			if (c == '\\' && pp_ucn_length(s, n, i)) {
				i += pp_ucn_length(s, n, i) - 1;
				continue;
			}
			if (!pp_character_is_identifier(c) && c != '.')
				return -1;
		}
		return LEX_TOKEN_NUMBER;
	}
	for (i = 0; i < sizeof(pp_punctuator_table) / sizeof(pp_punctuator_table[0]); i++)
		if (strlen(pp_punctuator_table[i].s) == n && !memcmp(pp_punctuator_table[i].s, s, n))
			return pp_punctuator_table[i].k;
	return -1;
}

static bool pp_paste_is_valid(const ptok *l, const ptok *r)
{
	char b[l->len + r->len + 1], *w = pp_token_spell_to(b, l);

	w = pp_token_spell_to(w, r);
	return pp_paste_result_classify(b, (size_t)(w - b), r->kind) >= 0;
}

static ptok pp_paste_tokens(pp *p, const ptok *l, const ptok *r)
{
	char *b = varena_alloc(&p->tmp, l->len + r->len + 24), *w;
	ptok t = *l;
	int k;
	uint32_t ll;

	w = pp_token_spell_to_padded(b, l);	/* (24 to spare) */
	ll = (uint32_t)(w - b);
	w = pp_token_spell_to_padded(w, r);
	memset(w, 0, 17);
	k = pp_paste_result_classify(b, (size_t)(w - b), r->kind);
	if (p->o->no_digraphs && ((w - b == 2 &&
	    ((b[0] == '<' && (b[1] == ':' || b[1] == '%')) ||
	     (b[0] == ':' && b[1] == '>') ||
	     (b[0] == '%' && (b[1] == '>' || b[1] == ':')))) ||
	    (w - b == 4 && !memcmp(b, "%:%:", 4))))
		k = -1;
	if (k < 0) {
		PP_ERROR(p, l->loc, "pasting formed '%.*s', an invalid preprocessing token", (int)(w - b), b);
		return *l;
	}
	t.s = b;
	t.len = (uint32_t)(w - b);
	t.kind = (uint8_t)k;
	t.flags = l->flags & (LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE | PP_TOKEN_FLAG_FROM_MACRO);	/* (PP_TOKEN_FLAG_FROM_MACRO: diagnostics) */
	t.id = k == LEX_TOKEN_IDENTIFIER ? vintern_copy(b, t.len) : 0;
	if (unlikely(r->ctr) && p->rec.on)
		t.ctr = pp_header_cache_counter_provenance_chain(p, l->ctr, 0, pp_header_cache_counter_provenance_chain(p, r->ctr, ll, 0));
	return t;
}

/* ---------------- expansion ---------------- */

static inline bool pp_paste_operator_is(const ptok *t)
{
	return t->kind == LEX_TOKEN_HASH_HASH && !(t->flags & PP_TOKEN_FLAG_NO_PASTE);
}

/* clang's maybeRemoveCommaBeforeVaArgs (gnu mode), only if the argument was elided */
static void pp_variadic_comma_remove(pp *p, size_t base, bool *ngs)
{
	if (p->scratch_token_count == base || p->sv[p->scratch_token_count - 1].kind != LEX_TOKEN_COMMA)
		return;
	p->scratch_token_count--;
	if (p->scratch_token_count > base && pp_paste_operator_is(&p->sv[p->scratch_token_count - 1]))
		p->scratch_token_count--;
	*ngs = false;
}

/* append n tokens, turning ## into non-pasting ones */
static size_t pp_tokens_append_unpasted(pp *p, const ptok *t, uint32_t n)
{
	size_t first = p->scratch_token_count;
	ptok *w = p->sv + first;

	if (unlikely(n > PP_SCRATCH_TOKENS_MAX - p->scratch_token_count)) {
		PP_FATAL(p, 0, "macro expansion too large");
	}
	for (uint32_t i = 0; i < n; i++) {
		w[i] = t[i];
		if (unlikely(t[i].kind == LEX_TOKEN_HASH_HASH))
			w[i].flags |= PP_TOKEN_FLAG_NO_PASTE;
	}
	p->scratch_token_count = first + n;
	return first;
}

/* substitute arguments into m's body (clang's ExpandFunctionArguments), paste, push */
static uint32_t pp_va_opt_expand(pp *p, const pp_macro *m, pp_macro_arguments *a, uint32_t i, uint32_t to, size_t base,
		       uint32_t eloc, bool *ngsp, bool str);

/* substitute body[from, to) into sv (clang's ExpandFunctionArguments) */
static void pp_macro_body_substitute_range(pp *p, const pp_macro *m, pp_macro_arguments *a, uint32_t from, uint32_t to, size_t base,
			uint32_t eloc, bool *ngsp)
{
	const lex_token *body = m->body;
	const uint8_t *pidx = m->pidx;
	uint32_t np = m->nparam;
	bool ngs = *ngsp, va = m->flags & PP_MACRO_FLAG_VARIADIC;
	ptok t;

	bool special = m->flags & (PP_MACRO_FLAG_VA_OPT | PP_MACRO_FLAG_HAS_HASH);

	for (uint32_t i = from; i < to; i++) {
		const lex_token *cur = &body[i];
		uint32_t k = pidx ? pidx[i] : 0;
		bool before_nonempty, before, after;

		if (i && (cur->flags & LEX_TOKEN_FLAG_SPACE_BEFORE))
			ngs = true;
		if (!special)
			goto plain;	/* no __VA_OPT__, no '#' */
		if (unlikely(cur->kind == LEX_TOKEN_IDENTIFIER && cur->id == KW_VA_OPT && va)) {
			i = pp_va_opt_expand(p, m, a, i, to, base, eloc, &ngs, false);
			continue;
		}
		if (unlikely(cur->kind == LEX_TOKEN_HASH && m->kind == PP_MACRO_FUNCTION_LIKE && i + 1 < to &&
			     body[i + 1].kind == LEX_TOKEN_IDENTIFIER && body[i + 1].id == KW_VA_OPT && va)) {
			i = pp_va_opt_expand(p, m, a, i + 1, to, base, eloc, &ngs, true);
			continue;
		}
		if (cur->kind == LEX_TOKEN_HASH && m->kind == PP_MACRO_FUNCTION_LIKE && i + 1 < to && pidx && pidx[i + 1]) {
			uint32_t j = pidx[i + 1] - 1;

			pp_macro_argument_stringify(p, a->tok + a->st[j], a->ln[j], &t);
			t.loc = eloc;
			if (ngs)
				t.flags |= LEX_TOKEN_FLAG_SPACE_BEFORE;
			pp_scratch_token_push(p, &t);
			i++;
			ngs = false;
			continue;
		}
plain:
		if (!k) {
			ptok *w;

			if (unlikely(p->scratch_token_count == PP_SCRATCH_TOKENS_MAX)) {
				PP_FATAL(p, 0, "macro expansion too large");
			}
			w = &p->sv[p->scratch_token_count++];
			pp_token_fill_from_lexer(w, cur, m->src, eloc);
			w->flags = (w->flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | (ngs ? LEX_TOKEN_FLAG_SPACE_BEFORE : 0) | PP_TOKEN_FLAG_FROM_MACRO;
			ngs = false;
			continue;
		}
		before_nonempty = p->scratch_token_count > base && pp_paste_operator_is(&p->sv[p->scratch_token_count - 1]);
		before = i && body[i - 1].kind == LEX_TOKEN_HASH_HASH;
		after = i + 1 < to && body[i + 1].kind == LEX_TOKEN_HASH_HASH;
		k--;
		if (!before && !after) {
			const ptok *at = a->pre[k];
			uint32_t an = a->preexpanded_lengths[k];

			if (an) {
				size_t f = pp_tokens_append_unpasted(p, at, an);

				p->sv[f].flags = (p->sv[f].flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | (ngs ? LEX_TOKEN_FLAG_SPACE_BEFORE : 0);
				ngs = false;
			}
			continue;
		}
		/* operand of ##: the unexpanded argument */
		if (a->ln[k]) {
			bool pseudo = false;
			size_t f;

			if (before_nonempty && p->scratch_token_count - base >= 2 && p->sv[p->scratch_token_count - 2].kind == LEX_TOKEN_COMMA &&
			    va && k == np - 1) {
				pseudo = true;	/* GNU ", ## __VA_ARGS__" with arguments */
				p->scratch_token_count--;
			}
			f = pp_tokens_append_unpasted(p, a->tok + a->st[k], a->ln[k]);
			/* clang: unexpanded argument tokens take the expansion's location */
			for (size_t j = f; j < p->scratch_token_count; j++)
				p->sv[j].loc = eloc, p->sv[j].flags |= PP_TOKEN_FLAG_FROM_MACRO;
			/* clang: no space after a ## ("A ## ARG"): an assembler's invalid paste stays ". ## foo" -> ".foo" */
			if (!pseudo)
				p->sv[f].flags = (p->sv[f].flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | (ngs && !before ? LEX_TOKEN_FLAG_SPACE_BEFORE : 0);
			ngs = false;
			continue;
		}
		/* empty operand: placemarker */
		if (after) {
			i++;
			continue;
		}
		if (before_nonempty)
			p->scratch_token_count--;
		if (before)
			ngs = false;	/* clang: a placemarker pasted on gives no space */
		if (va && k == np - 1 && a->variadic_elided)
			pp_variadic_comma_remove(p, base, &ngs);
	}
	*ngsp = ngs;
}

/*
 * __VA_OPT__(...) at body[i] (C23 6.10.5.1): the content if the variadic
 * argument is non-empty after expansion, else a placemarker.  With str,
 * the result is stringified (#__VA_OPT__).  Returns the index of its ')'.
 */
static uint32_t pp_va_opt_expand(pp *p, const pp_macro *m, pp_macro_arguments *a, uint32_t i, uint32_t to, size_t base,
		       uint32_t eloc, bool *ngsp, bool str)
{
	const lex_token *body = m->body;
	uint32_t k = m->nparam - 1, b = i + 2, e = b, depth = 0;
	size_t start = p->scratch_token_count;
	bool before = i && body[i - 1].kind == LEX_TOKEN_HASH_HASH && !str;

	for (; e < to; e++) {
		if (body[e].kind == LEX_TOKEN_LEFT_PAREN)
			depth++;
		else if (body[e].kind == LEX_TOKEN_RIGHT_PAREN && !depth--)
			break;
	}
	if (i + 1 >= to || body[i + 1].kind != LEX_TOKEN_LEFT_PAREN || e >= to) {
		PP_ERROR(p, eloc, "__VA_OPT__ must be followed by a parenthesized list");
		return to;
	}
	if (!a->preexpanded_state[k])
		pp_macro_argument_preexpand(p, a, k, false);
	if (a->preexpanded_lengths[k]) {
		bool ngs = false;

		pp_macro_body_substitute_range(p, m, a, b, e, start, eloc, &ngs);
	}
	if (str) {
		ptok t;
		uint32_t n;
		const ptok *r = pp_scratch_tokens_take(p, start, &n);

		pp_macro_argument_stringify(p, r, n, &t);
		t.loc = eloc;
		if (*ngsp)
			t.flags |= LEX_TOKEN_FLAG_SPACE_BEFORE;
		pp_scratch_token_push(p, &t);
	} else if (p->scratch_token_count == start) {
		/* placemarker: a ## pm ## d is a ## d; pm ## d is d; a ## pm is a */
		if (e + 1 < to && body[e + 1].kind == LEX_TOKEN_HASH_HASH)
			e++;
		else if (before && p->scratch_token_count > base && pp_paste_operator_is(&p->sv[p->scratch_token_count - 1]))
			p->scratch_token_count--;
	} else {
		p->sv[start].flags = (p->sv[start].flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | (*ngsp ? LEX_TOKEN_FLAG_SPACE_BEFORE : 0);
	}
	*ngsp = false;
	return e;
}

/*
 * pp_macro_body_substitute() for a function-like macro without '#', '##' or __VA_OPT__ (most):
 * every parameter is replaced by its pre-expanded argument, so the result's
 * size is known and it is built in place, without sv.
 */
static void pp_macro_body_substitute_simple(pp *p, const pp_macro *m, pp_macro_arguments *a, uint8_t nf, uint32_t eloc)
{
	const lex_token *body = m->body;
	const uint8_t *pidx = m->pidx;
	const char *src = m->src;
	uint32_t nb = m->nbody, n = 0;
	bool ngs = false;
	ptok *r, *w;

	if (pidx) {
		for (uint32_t i = 0; i < nb; i++) {
			uint32_t k = pidx[i];

			if (!k) {
				n++;
				continue;
			}
			if (!a->preexpanded_state[k - 1])
				pp_macro_argument_preexpand(p, a, k - 1, true);
			n += a->preexpanded_lengths[k - 1];
		}
	} else {
		n = nb;
	}
	if (!n) {
		p->pend |= nf, p->cmt_sol |= nf & LEX_TOKEN_FLAG_LINE_START, p->sol_on = p->sol_on && p->line_start_context_depth != p->depth;
		return;
	}
	r = w = varena_alloc(&p->tmp, n * sizeof(ptok));
	for (uint32_t i = 0; i < nb; i++) {
		const lex_token *cur = &body[i];
		uint32_t k = pidx ? pidx[i] : 0;

		if (i && (cur->flags & LEX_TOKEN_FLAG_SPACE_BEFORE))
			ngs = true;
		if (!k) {
			pp_token_fill_from_lexer(w, cur, src, eloc);
			w->flags = (w->flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | (w == r ? nf : ngs ? LEX_TOKEN_FLAG_SPACE_BEFORE : 0) | PP_TOKEN_FLAG_FROM_MACRO;
			ngs = false;
			w++;
			continue;
		}
		{
			const ptok *at = a->pre[k - 1];
			uint32_t an = a->preexpanded_lengths[k - 1];

			if (!an)
				continue;
			for (uint32_t j = 0; j < an; j++) {
				w[j] = at[j];
				if (unlikely(at[j].kind == LEX_TOKEN_HASH_HASH))
					w[j].flags |= PP_TOKEN_FLAG_NO_PASTE;
			}
			w->flags = (w->flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | (w == r ? nf : ngs ? LEX_TOKEN_FLAG_SPACE_BEFORE : 0);
			ngs = false;
			w += an;
		}
	}
	pp_context_tokens_push(p, r, n, m->name, 0, 0);	/* r[0] took the name's SOL/SPACE (the context's "first") */
	p->cx[p->depth - 1].trailing_space = ngs;
	pp_macro_table_disable(p, m->name);
}

static int pp_macro_body_substitute(pp *p, const pp_macro *m, const ptok *name, pp_macro_arguments *a, uint32_t eloc)
{
	const lex_token *body = m->body;
	const uint8_t *pidx = m->pidx;
	uint32_t nb = m->nbody, n;
	uint8_t nf = name->flags & (LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE);
	bool ngs = false;
	size_t base;
	ptok *r;

	if (a && !(m->flags & (PP_MACRO_FLAG_PASTE | PP_MACRO_FLAG_VA_OPT | PP_MACRO_FLAG_HAS_HASH))) {
		pp_macro_body_substitute_simple(p, m, a, nf, eloc);
		return 0;
	}
	/* pre-expand first: it pushes contexts and uses sv */
	if (a && pidx) {
		for (uint32_t i = 0; i < nb; i++) {
			uint32_t k = pidx[i];

			if (!k || a->preexpanded_state[k - 1])
				continue;
			if (i && body[i - 1].kind == LEX_TOKEN_HASH)
				continue;
			if ((i && body[i - 1].kind == LEX_TOKEN_HASH_HASH) || (i + 1 < nb && body[i + 1].kind == LEX_TOKEN_HASH_HASH))
				continue;
			pp_macro_argument_preexpand(p, a, k - 1, true);
		}
	}
	base = p->scratch_token_count;
	pp_macro_body_substitute_range(p, m, a, 0, nb, base, eloc, &ngs);
	/* token pasting, left to right */
	if (m->flags & PP_MACRO_FLAG_PASTE) {
		size_t w = base, e = p->scratch_token_count;

		for (size_t i = base; i < e;) {
			ptok cur;

			if (!(i + 2 < e && pp_paste_operator_is(&p->sv[i + 1]))) {
				/* not pasted: stays (moves down once something was) */
				if (w != i)
					p->sv[w] = p->sv[i];
				w++, i++;
				continue;
			}
			cur = p->sv[i++];

			while (i + 1 < e && pp_paste_operator_is(&p->sv[i])) {
				ptok r;

				if (p->o->asm_mode && !pp_paste_is_valid(&cur, &p->sv[i + 1])) {
					/* assembler: an invalid paste leaves both tokens */
					p->sv[w++] = cur;
					cur = p->sv[i + 1];
					i += 2;
					continue;
				}
				r = pp_paste_tokens(p, &cur, &p->sv[i + 1]);
				cur = r;
				i += 2;
			}
			p->sv[w++] = cur;
		}
		p->scratch_token_count = w;
	}
	r = pp_scratch_tokens_take(p, base, &n);
	if (!n) {
		p->pend |= nf, p->cmt_sol |= nf & LEX_TOKEN_FLAG_LINE_START, p->sol_on = p->sol_on && p->line_start_context_depth != p->depth;
		return 0;
	}
	/* the context's "first" (the name's SOL/SPACE), applied now: r is ours */
	r[0].flags = (r[0].flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | nf;
	pp_context_tokens_push(p, r, n, m->name, 0, 0);
	p->cx[p->depth - 1].trailing_space = ngs;
	pp_macro_table_disable(p, m->name);
	return 0;
}

/* clang's isTrivialSingleTokenExpansion */
static bool pp_macro_is_trivial(pp *p, const pp_macro *m)
{
	const lex_token *v = &m->body[0];
	uintptr_t e;

	if (v->kind != LEX_TOKEN_IDENTIFIER)
		return true;
	e = pp_macro_read(p, v->id);
	if (e && !(e & 1) && v->id != m->name)
		return false;
	if (m->kind == PP_MACRO_OBJECT_LIKE)
		return true;
	return !m->pidx || !m->pidx[0];
}

static void pp_macro_single_token_expand(pp *p, const pp_macro *m, ptok *t, uint8_t nf, uint32_t eloc)
{
	uintptr_t e;

	pp_token_fill_from_lexer(t, &m->body[0], m->src, eloc);
	t->flags |= PP_TOKEN_FLAG_FROM_MACRO;
	t->flags = (t->flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | nf;
	if (t->kind == LEX_TOKEN_IDENTIFIER && (e = pp_macro_read(p, t->id)) && ((e & 1) || (const pp_macro *)(e & ~(uintptr_t)1) == m))
		t->flags |= PP_TOKEN_FLAG_NO_EXPAND;
}

/* -E text: before a macro name read from a file is expanded (its column, -C's comments) */
static __attribute__((noinline, cold)) void pp_macro_text_position_note(pp *p, ptok *t)
{
	pp_context *fc = &p->cx[p->depth - 1];

	p->expansion_location = t->loc;	/* clang's expansion location: where the outermost name is */
	if ((t->flags & (LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) == (LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE) && !p->sol_on && pp_token_column(p, t->loc) == 1)
		t->flags &= ~LEX_TOKEN_FLAG_SPACE_BEFORE;	/* clang: a line break is no leading space */
	if (unlikely(p->output_text_mode > 1) && !(t->flags & PP_TOKEN_FLAG_FROM_MACRO) && p->fe[fc->fe].comment_position && t->s >= p->fe[fc->fe].comment_position &&
	    t->s < fc->src + fc->f->size) {
		pp_comments_flush(p, fc, t->s);	/* -C: the comments before it, not those in its arguments */
		p->fe[fc->fe].comment_position = t->s + t->len;
		if (p->previous_end == t->s)
			t->flags &= ~LEX_TOKEN_FLAG_SPACE_BEFORE;	/* right after a comment */
	}
}

/* expand macro m named by t; 1 if t now holds the result token, 2 if the next raw one */
VPRIV int pp_macro_expand(pp *p, const pp_macro *m, ptok *t)
{
	pp_macro_arguments a;
	ptok rp;

	if (unlikely(p->output_text_mode) && !p->in_directive && p->cx[p->depth - 1].type == PP_CONTEXT_FILE)
		pp_macro_text_position_note(p, t);
	uint8_t nf = t->flags & (LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE);

	if ((nf & LEX_TOKEN_FLAG_LINE_START) && !p->sol_on && !p->in_directive && p->cx[p->depth - 1].type == PP_CONTEXT_FILE)
		p->line_start_location = t->loc, p->sol_on = 1, p->line_start_context_depth = p->depth;

	if (m->kind == PP_MACRO_BUILTIN)
		return pp_builtin_macro_expand(p, m, t);
	if (m->kind == PP_MACRO_OBJECT_LIKE) {
		pp_context *c;

		if (!m->nbody) {
			p->pend |= nf, p->cmt_sol |= nf & LEX_TOKEN_FLAG_LINE_START, p->sol_on = p->sol_on && p->line_start_context_depth != p->depth;
			return 0;
		}
		if (m->nbody == 1 && pp_macro_is_trivial(p, m)) {
			pp_macro_single_token_expand(p, m, t, nf, t->loc);
			return 1;
		}
		if (m->flags & PP_MACRO_FLAG_PASTE)
			return pp_macro_body_substitute(p, m, t, NULL, t->loc);
		c = pp_context_push(p);
		c->type = PP_CONTEXT_LEXED;
		c->vt = m->body;
		c->n = m->nbody;
		c->src = m->src;
		c->loc = t->loc;
		c->mid = m->name;
		pp_macro_table_disable(p, m->name);
		/* its first token, as pp_token_raw_next() would read it next (the context's "first" applied) */
		c->i = 1;
		pp_token_fill_from_lexer(t, &m->body[0], m->src, c->loc);
		t->flags = (t->flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | nf | p->pend;
		p->pend = 0;
		return 2;
	}
	if (!pp_macro_arguments_collect(p, m, t, &a, &rp))
		return pp_macro_arguments_collect_fail(t, &rp);
	if (unlikely(p->output_text_mode > 1))
		pp_comments_skip_to(p, &rp);	/* -C: comments in macro arguments are dropped */
	if (!m->nbody) {
		p->scratch_token_count = a.base;
		p->pend |= nf, p->cmt_sol |= nf & LEX_TOKEN_FLAG_LINE_START, p->sol_on = p->sol_on && p->line_start_context_depth != p->depth;
		return 0;
	}
	if (m->nbody == 1 && pp_macro_is_trivial(p, m)) {
		p->scratch_token_count = a.base;
		pp_macro_single_token_expand(p, m, t, nf, rp.loc);
		return 1;
	}
	pp_macro_body_substitute(p, m, t, &a, rp.loc);
	p->scratch_token_count = a.base;
	return 0;
}
