/* vcc preprocessor: #if expressions */
#include "pp-internal.h"

VPRIV pp_if_value pp_number_parse(pp *p, const ptok *t)
{
	char tmp[t->len + 1];
	size_t n, i = 0;
	const char *s = pp_token_spell(t, tmp, &n);
	unsigned base = 10;
	uint64_t v = 0;
	bool over = false, u = false;
	pp_if_value r;

	if (n > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		i = 2;
	} else if (n > 1 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
		base = 2;
		i = 2;
	} else if (s[0] == '0') {
		base = 8;
	}
	for (; i < n; i++) {
		unsigned d;
		char c = s[i];

		if (c == '\'')
			continue;
		if (c >= '0' && c <= '9')
			d = (unsigned)(c - '0');
		else if (base == 16 && c >= 'a' && c <= 'f')
			d = (unsigned)(c - 'a' + 10);
		else if (base == 16 && c >= 'A' && c <= 'F')
			d = (unsigned)(c - 'A' + 10);
		else
			break;
		if (d >= base)
			break;
		if (v > (UINT64_MAX - d) / base)
			over = true;
		v = v * base + d;
	}
	for (; i < n; i++) {
		char c = s[i];

		if (c == 'u' || c == 'U')
			u = true;
		else if (c != 'l' && c != 'L') {
			PP_ERROR(p, t->loc, "invalid integer constant '%.*s' in preprocessor expression", (int)n, s);
			break;
		}
	}
	if (over)
		PP_ERROR(p, t->loc, "integer literal is too large");
	r.v = v;
	r.u = u || v > INT64_MAX;
	return r;
}

static pp_if_value pp_character_constant_parse(pp *p, const ptok *t)
{
	char tmp[t->len + 1];
	size_t n;
	const char *s = pp_token_spell(t, tmp, &n), *e;
	uint64_t v = 0;
	unsigned cnt = 0;
	bool narrow = t->kind == LEX_TOKEN_CHARACTER || t->kind == LEX_TOKEN_UTF8_CHARACTER;
	pp_if_value r;

	while (*s != '\'') {
		s++;
		n--;
	}
	e = s + n - 1;
	for (s++; s < e; cnt++) {
		uint64_t c;

		if (*s == '\\') {
			s++;
			switch (*s) {
			case 'n': c = '\n'; s++; break;
			case 't': c = '\t'; s++; break;
			case 'r': c = '\r'; s++; break;
			case 'a': c = 7; s++; break;
			case 'b': c = 8; s++; break;
			case 'f': c = 12; s++; break;
			case 'v': c = 11; s++; break;
			case 'e': c = 27; s++; break;
			case 'x':
				c = 0;
				for (s++; s < e && strchr("0123456789abcdefABCDEF", *s); s++)
					c = c * 16 + (unsigned)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
				break;
			default:
				if (*s >= '0' && *s <= '7') {
					c = 0;
					for (int k = 0; k < 3 && s < e && *s >= '0' && *s <= '7'; k++, s++)
						c = c * 8 + (unsigned)(*s - '0');
				} else {
					c = (unsigned char)*s++;
				}
			}
		} else {
			c = (unsigned char)*s++;
		}
		v = narrow ? (v << 8) | (c & 0xff) : c;
	}
	if (cnt > 1 && t->kind == LEX_TOKEN_CHARACTER) {
		diag_span sp;
		diag_info d = { "warning", t->loc, &sp, 0, NULL, 0, NULL, 0 };

		if (cnt != 4)	/* (4: -Wfour-char-constants, off) */
			diag_warning_report(p, "multichar", true, NULL, &d, "multi-character character constant");
		if (cnt > 4)
			diag_report(p, t->loc, "warning", "character constant too long for its type");
	}
	if (narrow && cnt == 1 && t->kind == LEX_TOKEN_CHARACTER && !p->options->unsigned_char)
		v = (uint64_t)(int64_t)(int8_t)v;
	else if (narrow && cnt > 1)
		v = (uint64_t)(int64_t)(int32_t)v;
	r.v = v;
	r.u = false;
	return r;
}

typedef struct pp_if_evaluation {
	pp *p;
	ptok lookahead;		/* lookahead */
	uint32_t loc;
	uint32_t previous_location;		/* the last token consumed */
	uint32_t primary_first, primary_last;	/* the last primary expression: first and last token */
	bool failed;
} pp_if_evaluation;

static void pp_if_token_advance(pp_if_evaluation *e)
{
	e->previous_location = e->lookahead.loc;
	pp_token_expanded_next(e->p, &e->lookahead);
}

static pp_if_value pp_if_comma_parse(pp_if_evaluation *e, bool live);

static pp_if_value pp_if_primary_parse_inner(pp_if_evaluation *e, bool live);
static pp_if_value pp_if_primary_parse(pp_if_evaluation *e, bool live)
{
	uint32_t b = e->lookahead.loc;
	pp_if_value r = pp_if_primary_parse_inner(e, live);

	e->primary_first = b, e->primary_last = e->previous_location;
	return r;
}

static pp_if_value pp_if_primary_parse_inner(pp_if_evaluation *e, bool live)
{
	pp_if_value r = { 0, false };
	pp *p = e->p;

	if (vstack_guard_low())
		PP_FATAL(p, e->lookahead.loc, "#if expression nested too deeply");
	switch (e->lookahead.kind) {
	case LEX_TOKEN_NUMBER:
		r = pp_number_parse(p, &e->lookahead);
		pp_if_token_advance(e);
		return r;
	case LEX_TOKEN_CHARACTER: case LEX_TOKEN_WIDE_CHARACTER: case LEX_TOKEN_UTF8_CHARACTER: case LEX_TOKEN_UTF16_CHARACTER: case LEX_TOKEN_UTF32_CHARACTER:
		r = pp_character_constant_parse(p, &e->lookahead);
		pp_if_token_advance(e);
		return r;
	case LEX_TOKEN_LEFT_PAREN:
		pp_if_token_advance(e);
		r = pp_if_comma_parse(e, live);
		if (e->lookahead.kind != LEX_TOKEN_RIGHT_PAREN) {
			if (!e->failed)
				PP_ERROR(p, e->loc, "expected ')' in preprocessor expression");
			e->failed = true;
			return r;
		}
		pp_if_token_advance(e);
		return r;
	case LEX_TOKEN_PLUS:
		pp_if_token_advance(e);
		return pp_if_primary_parse(e, live);
	case LEX_TOKEN_MINUS:
		pp_if_token_advance(e);
		r = pp_if_primary_parse(e, live);
		r.v = -r.v;
		return r;
	case LEX_TOKEN_TILDE:
		pp_if_token_advance(e);
		r = pp_if_primary_parse(e, live);
		r.v = ~r.v;
		return r;
	case LEX_TOKEN_EXCLAMATION:
		pp_if_token_advance(e);
		r = pp_if_primary_parse(e, live);
		r.v = !r.v;
		r.u = false;
		return r;
	case LEX_TOKEN_IDENTIFIER:
		if (e->lookahead.id == KW_defined) {
			ptok n;
			bool paren;

			if (unlikely(pp_location_is_object_macro(p, e->lookahead.loc))) {
				diag_info d = { "warning", e->lookahead.loc, NULL, 0, NULL, 0, "defined", 7 };

				diag_warning_report(p, "expansion-to-defined", true, NULL, &d,
					 "macro expansion producing 'defined' has undefined behavior");
			}

			pp_token_raw_next(p, &n);
			paren = n.kind == LEX_TOKEN_LEFT_PAREN;
			if (paren)
				pp_token_raw_next(p, &n);
			if (n.kind != LEX_TOKEN_IDENTIFIER) {
				PP_ERROR(p, e->loc, "macro name must be an identifier");
				e->failed = true;
				return r;
			}
			r.v = pp_macro_is_defined(p, n.id);
			if (paren) {
				pp_token_raw_next(p, &n);
				if (n.kind != LEX_TOKEN_RIGHT_PAREN) {
					PP_ERROR(p, e->loc, "missing ')' after 'defined'");
					e->failed = true;
				}
			}
			pp_if_token_advance(e);
			return r;
		}
		if ((p->options->c23 || p->options->cxx) && e->lookahead.id == KW_true)
			r.v = 1;	/* C23 and C++: true is 1 in #if (false is 0 anyway) */
		{
			uint32_t id = e->lookahead.id, loc = e->lookahead.loc;

			pp_if_token_advance(e);	/* an identifier that is not a macro: 0 */
			if (p->options->c23 && e->lookahead.kind == LEX_TOKEN_CHARACTER && e->lookahead.loc == loc + 2 && vintern_ents[id].len == 2 &&
			    !memcmp(vintern_ents[id].s, "u8", 2)) {
				/* C23 u8'a' (the lexer's C17 tokens u8 and 'a') */
				e->lookahead.kind = LEX_TOKEN_UTF8_CHARACTER;
				r = pp_character_constant_parse(p, &e->lookahead);
				pp_if_token_advance(e);
				return r;
			}
			if (e->lookahead.kind == LEX_TOKEN_LEFT_PAREN && !e->failed) {
				PP_ERROR(p, loc, "function-like macro '%.*s' is not defined",
				      (int)vintern_ents[id].len, vintern_ents[id].s);
				e->failed = true;
			}
		}
		return r;
	default:
		if (!e->failed)
			PP_ERROR(p, e->loc, "invalid token at start of a preprocessor expression");
		e->failed = true;
		return r;
	}
}

static int pp_if_operator_precedence(int k)
{
	switch (k) {
	case LEX_TOKEN_STAR: case LEX_TOKEN_SLASH: case LEX_TOKEN_PERCENT: return 10;
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: return 9;
	case LEX_TOKEN_LESS_LESS: case LEX_TOKEN_GREATER_GREATER: return 8;
	case LEX_TOKEN_LESS: case LEX_TOKEN_GREATER: case LEX_TOKEN_LESS_EQUAL: case LEX_TOKEN_GREATER_EQUAL: return 7;
	case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_EXCLAMATION_EQUAL: return 6;
	case LEX_TOKEN_AMPERSAND: return 5;
	case LEX_TOKEN_CARET: return 4;
	case LEX_TOKEN_PIPE: return 3;
	case LEX_TOKEN_AMPERSAND_AMPERSAND: return 2;
	case LEX_TOKEN_PIPE_PIPE: return 1;
	}
	return 0;
}

static pp_if_value pp_if_binary_parse(pp_if_evaluation *e, int minp, bool live)
{
	pp_if_value l = pp_if_primary_parse(e, live);

	for (;;) {
		int op = e->lookahead.kind, pr = pp_if_operator_precedence(op);
		pp_if_value r;
		bool u;

		if (!pr || pr < minp || e->failed)
			return l;
		pp_if_token_advance(e);
		if (op == LEX_TOKEN_AMPERSAND_AMPERSAND) {
			r = pp_if_binary_parse(e, pr + 1, live && l.v);
			l.v = l.v && r.v;
			l.u = false;
			continue;
		}
		if (op == LEX_TOKEN_PIPE_PIPE) {
			r = pp_if_binary_parse(e, pr + 1, live && !l.v);
			l.v = l.v || r.v;
			l.u = false;
			continue;
		}
		r = pp_if_binary_parse(e, pr + 1, live);
		u = l.u || r.u;
		switch (op) {
		case LEX_TOKEN_STAR: l.v = l.v * r.v; break;
		case LEX_TOKEN_SLASH: case LEX_TOKEN_PERCENT:
			if (!r.v) {
				if (live) {
					PP_ERROR(e->p, e->loc, "division by zero in preprocessor expression");
					e->failed = true;
				}
				l.v = 0;
				break;
			}
			if (u)
				l.v = op == LEX_TOKEN_SLASH ? l.v / r.v : l.v % r.v;
			else if ((int64_t)r.v == -1)
				l.v = op == LEX_TOKEN_SLASH ? -l.v : 0;
			else
				l.v = op == LEX_TOKEN_SLASH ? (uint64_t)((int64_t)l.v / (int64_t)r.v)
						    : (uint64_t)((int64_t)l.v % (int64_t)r.v);
			break;
		case LEX_TOKEN_PLUS: l.v = l.v + r.v; break;
		case LEX_TOKEN_MINUS: l.v = l.v - r.v; break;
		case LEX_TOKEN_LESS_LESS:
			l.v = r.v >= 64 ? 0 : l.v << r.v;
			u = l.u;
			break;
		case LEX_TOKEN_GREATER_GREATER:
			if (r.v >= 64)
				l.v = l.u || (int64_t)l.v >= 0 ? 0 : ~0ull;
			else
				l.v = l.u ? l.v >> r.v : (uint64_t)((int64_t)l.v >> r.v);
			u = l.u;
			break;
		case LEX_TOKEN_LESS: l.v = u ? l.v < r.v : (int64_t)l.v < (int64_t)r.v; u = false; break;
		case LEX_TOKEN_GREATER: l.v = u ? l.v > r.v : (int64_t)l.v > (int64_t)r.v; u = false; break;
		case LEX_TOKEN_LESS_EQUAL: l.v = u ? l.v <= r.v : (int64_t)l.v <= (int64_t)r.v; u = false; break;
		case LEX_TOKEN_GREATER_EQUAL: l.v = u ? l.v >= r.v : (int64_t)l.v >= (int64_t)r.v; u = false; break;
		case LEX_TOKEN_EQUAL_EQUAL: l.v = l.v == r.v; u = false; break;
		case LEX_TOKEN_EXCLAMATION_EQUAL: l.v = l.v != r.v; u = false; break;
		case LEX_TOKEN_AMPERSAND: l.v &= r.v; break;
		case LEX_TOKEN_CARET: l.v ^= r.v; break;
		case LEX_TOKEN_PIPE: l.v |= r.v; break;
		}
		l.u = u;
	}
}

static pp_if_value pp_if_conditional_parse(pp_if_evaluation *e, bool live)
{
	pp_if_value c = pp_if_binary_parse(e, 1, live), a, b;

	if (e->lookahead.kind != LEX_TOKEN_QUESTION || e->failed)
		return c;
	pp_if_token_advance(e);
	a = pp_if_comma_parse(e, live && c.v);
	if (e->lookahead.kind != LEX_TOKEN_COLON) {
		if (!e->failed)
			PP_ERROR(e->p, e->loc, "expected ':' in preprocessor expression");
		e->failed = true;
		return a;
	}
	pp_if_token_advance(e);
	b = pp_if_conditional_parse(e, live && !c.v);
	a = c.v ? a : b;
	a.u = a.u || b.u;
	return a;
}

static pp_if_value pp_if_comma_parse(pp_if_evaluation *e, bool live)
{
	pp_if_value r = pp_if_conditional_parse(e, live);

	while (e->lookahead.kind == LEX_TOKEN_COMMA && !e->failed) {
		pp_if_token_advance(e);
		r = pp_if_conditional_parse(e, live);
	}
	return r;
}

/* evaluate tokens [b, e) of fc's current file as an #if expression */
VPRIV bool pp_if_evaluate(pp *p, const pp_context *fc, uint32_t b, uint32_t e)
{
	int d = p->context_depth;
	pp_if_evaluation E = { .p = p, .loc = fc->location + fc->lexed_tokens[b > 0 ? b - 1 : 0].off };
	pp_if_value r;

	if (b >= e) {
		PP_ERROR(p, E.loc, "#if with no expression");
		return false;
	}
	pp_context_directive_line_push(p, fc, b, e);
	p->in_directive++;
	pp_if_token_advance(&E);
	r = pp_if_comma_parse(&E, true);
	p->in_directive--;
	if (!E.failed && E.lookahead.kind != LEX_TOKEN_EOF)
	{
		/* clang: at the token, the operand before it underlined */
		diag_span sp = { E.primary_first, pp_token_end_find(p, E.primary_last) };
		diag_info d = { "error", E.lookahead.loc, &sp, E.primary_first && E.primary_last >= E.primary_first, NULL, 0, NULL, 0 };
		char tmp[E.lookahead.len + 1];
		size_t ml;

		if (E.lookahead.flags & PP_TOKEN_FLAG_FROM_MACRO)	/* (the note at the macro's body token) */
			d.ms = pp_token_spell(&E.lookahead, tmp, &ml), d.mlen = (uint32_t)ml;

		diag_report_extended(p, &d, "token is not a valid binary operator in a preprocessor subexpression");
	}
	pp_context_pop_to(p, d);
	return r.v != 0;
}

/* a constant expression of already expanded tokens (#embed's limit()): false if not one */
VPRIV bool pp_if_tokens_evaluate(pp *p, const ptok *t, uint32_t n, uint64_t *v)
{
	int d = p->context_depth;
	pp_if_evaluation E = { .p = p, .loc = n ? t[0].loc : 0 };
	pp_if_value r;

	if (!n)
		return false;
	pp_context_tokens_push(p, t, n, 0, 0, 1);
	p->in_directive++;
	pp_if_token_advance(&E);
	r = pp_if_comma_parse(&E, true);
	p->in_directive--;
	if (E.lookahead.kind != LEX_TOKEN_EOF)
		E.failed = true;
	pp_context_pop_to(p, d);
	*v = (uint64_t)r.v;
	return !E.failed;
}
