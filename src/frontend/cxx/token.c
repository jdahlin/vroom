/*
 * token.c: the TU's tokens as one array (cxx-internal.h), C++'s joined
 * punctuators, keywords, tentative parsing and errors.
 *
 * The lexer has no C++ mode (PLAN-cxx.md X2): .* ->* <=> arrive as two
 * tokens and are joined here when they are adjacent in the same buffer,
 * which is maximal munch and, like clang, never joins across a macro
 * boundary (an expansion's tokens are not adjacent in memory to the next
 * file token).
 */
#include <stdarg.h>
#include <stdio.h>
#include "cxx-internal.h"

static const char *const cxx_keyword_names[] = {
	"",
#define CXX_KW_NAME(n) #n,
	CXX_KEYWORDS(CXX_KW_NAME)
#undef CXX_KW_NAME
};

/* id -> keyword: open addressing over the few hundred keyword ids */
#define CXX_KEYWORD_SLOTS 1024
static struct { uint32_t id; uint16_t kw; } cxx_keyword_table[CXX_KEYWORD_SLOTS];

static void cxx_keyword_table_init(void)
{
	for (int k = 1; k < CXX_KW_COUNT; k++) {
		uint32_t id = vintern_cstr(cxx_keyword_names[k]), h = (id * 2654435761u) % CXX_KEYWORD_SLOTS;

		while (cxx_keyword_table[h].id)
			h = (h + 1) % CXX_KEYWORD_SLOTS;
		cxx_keyword_table[h].id = id;
		cxx_keyword_table[h].kw = (uint16_t)k;
	}
}

VPRIV uint32_t cxx_keyword_of(uint32_t id)
{
	uint32_t h = (id * 2654435761u) % CXX_KEYWORD_SLOTS;

	while (cxx_keyword_table[h].id) {
		if (cxx_keyword_table[h].id == id)
			return cxx_keyword_table[h].kw;
		h = (h + 1) % CXX_KEYWORD_SLOTS;
	}
	return 0;
}

VPRIV void cxx_tokens_read(cxx *c)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	uint32_t cap = 1u << 16;
	ptok t;

	pthread_once(&once, cxx_keyword_table_init);
	/* the parser keeps every token: pp's made spellings (#x, a##b) must not be reused */
	pp_temporary_hold(c->p, 1);
	c->tok = malloc(cap * sizeof(cxx_token));
	c->ntok = 0;
	for (;;) {
		pp_token_next(c->p, &t);
		if (t.kind == PP_TOKEN_KIND_DIRECTIVE_LINE)
			continue;	/* #pragma lines: none matters to parsing json yet */
		if (c->ntok + 2 >= cap)
			c->tok = realloc(c->tok, (cap *= 2) * sizeof(cxx_token));
		cxx_token *x = &c->tok[c->ntok];
		x->s = t.s, x->len = t.len, x->id = t.id, x->loc = t.loc, x->kind = t.kind, x->kw = 0;
		/* >> >= >>= as one-character pieces: a > may close template arguments, and the token array
		   stays as read (an instantiation re-parses a template's tokens); the expression parser
		   joins adjacent pieces where they are operators (cxx_operator_at) */
		if (t.kind == LEX_TOKEN_GREATER_GREATER || t.kind == LEX_TOKEN_GREATER_EQUAL || t.kind == LEX_TOKEN_GREATER_GREATER_EQUAL) {
			static const char *const pieces = ">>=";
			const char *sp = t.kind == LEX_TOKEN_GREATER_EQUAL ? ">=" : t.kind == LEX_TOKEN_GREATER_GREATER ? ">>" : pieces;

			if (c->ntok + 4 >= cap)
				c->tok = realloc(c->tok, (cap *= 2) * sizeof(cxx_token));
			for (uint32_t k = 0; k < t.len; k++) {
				cxx_token *y = &c->tok[c->ntok++];

				y->s = t.s + k, y->len = 1, y->id = 0, y->loc = t.loc + k, y->kw = 0;
				y->kind = sp[k] == '>' ? LEX_TOKEN_GREATER : LEX_TOKEN_EQUAL;
			}
			continue;
		}
		if (t.kind == LEX_TOKEN_EOF)
			break;
		if (t.kind == LEX_TOKEN_IDENTIFIER)
			x->kw = (uint16_t)cxx_keyword_of(t.id);
		/* joins: the previous token and this one adjacent in memory */
		if (c->ntok) {
			cxx_token *pv = &c->tok[c->ntok - 1];
			bool adj = pv->s + pv->len == t.s;

			if (adj && t.kind == LEX_TOKEN_STAR && pv->kind == LEX_TOKEN_PERIOD) {
				pv->kind = CXX_TOKEN_PERIOD_STAR, pv->len++;
				continue;
			}
			if (adj && t.kind == LEX_TOKEN_STAR && pv->kind == LEX_TOKEN_ARROW) {
				pv->kind = CXX_TOKEN_ARROW_STAR, pv->len++;
				continue;
			}
			/* C++11 [lex.pptoken]: <:: not followed by : or > is < :: (std::vector<::T>) */
			if (adj && t.kind == LEX_TOKEN_COLON && pv->kind == LEX_TOKEN_LEFT_SQUARE && pv->len == 2 && pv->s[0] == '<' &&
			    t.s[1] != ':' && t.s[1] != '>') {
				pv->kind = LEX_TOKEN_LESS, pv->len = 1;
				x->kind = LEX_TOKEN_COLON_COLON, x->s = pv->s + 1, x->len = 2, x->loc = pv->loc + 1;
			}
			/* a user-defined literal: "abc"_json, 'c'_x (a number's suffix is already in its pp-number) */
			if (adj && t.kind == LEX_TOKEN_IDENTIFIER && pv->kind >= LEX_TOKEN_CHARACTER && pv->kind <= LEX_TOKEN_UTF32_STRING) {
				pv->len += t.len;
				continue;
			}
			if (adj && t.kind == LEX_TOKEN_GREATER && pv->kind == LEX_TOKEN_LESS_EQUAL) {
				pv->kind = CXX_TOKEN_SPACESHIP, pv->len++;
				continue;
			}
		}
		c->ntok++;
	}
	c->tok[c->ntok].kind = LEX_TOKEN_EOF;
	c->tok[c->ntok].kw = 0;
	c->tok[c->ntok + 1] = c->tok[c->ntok];
}

VPRIV const char *cxx_token_kind_spelling(int kind)
{
	if (kind == CXX_TOKEN_PERIOD_STAR) return ".*";
	if (kind == CXX_TOKEN_ARROW_STAR) return "->*";
	if (kind == CXX_TOKEN_SPACESHIP) return "<=>";
	return lex_token_name(kind);
}

VPRIV void cxx_fail(cxx *c, const char *fmt, ...)
{
	va_list ap;

	if (!c->tentative || c->pos >= c->error_pos) {
		va_start(ap, fmt);
		vsnprintf(c->error, sizeof c->error, fmt, ap);
		va_end(ap);
		c->error_pos = c->pos;
	}
	longjmp(*c->fail, 1);
}

VPRIV void cxx_expect(cxx *c, int kind)
{
	if (!cxx_accept(c, kind))
		cxx_fail(c, "expected '%s'", cxx_token_kind_spelling(kind));
}

VPRIV void cxx_expect_kw(cxx *c, int kw)
{
	if (!cxx_accept_kw(c, kw))
		cxx_fail(c, "expected '%s'", cxx_keyword_names[kw]);
}

/* the in-place edits a tentative parse may have to undo (none since >> is split at read; kept for the log) */
static __attribute__((unused)) void cxx_token_edit(cxx *c, uint32_t i, int kind, uint32_t skip)
{
	if (c->nundo == c->cundo)
		c->undo = realloc(c->undo, (c->cundo = c->cundo ? c->cundo * 2 : 64) * sizeof(cxx_undo));
	c->undo[c->nundo].index = i;
	c->undo[c->nundo++].token = c->tok[i];
	c->tok[i].kind = (uint16_t)kind;
	c->tok[i].s += skip;
	c->tok[i].len -= skip;
	c->tok[i].loc += skip;
}

VPRIV bool cxx_accept_greater(cxx *c)
{
	return cxx_accept(c, LEX_TOKEN_GREATER);	/* (>> and >= arrive as pieces: tokens_read) */
}

/* the operator here, joining > pieces that are adjacent in the source: its kind and token count */
VPRIV int cxx_operator_at(cxx *c, uint32_t *ntok)
{
	cxx_token *t = cxx_peek(c, 0), *n = cxx_peek(c, 1), *n2 = cxx_peek(c, 2);

	*ntok = 1;
	if (t->kind != LEX_TOKEN_GREATER || n->s != t->s + 1)
		return t->kind;
	if (n->kind == LEX_TOKEN_GREATER) {
		if (n2->kind == LEX_TOKEN_EQUAL && n2->s == n->s + 1) {
			*ntok = 3;
			return LEX_TOKEN_GREATER_GREATER_EQUAL;
		}
		*ntok = 2;
		return LEX_TOKEN_GREATER_GREATER;
	}
	if (n->kind == LEX_TOKEN_EQUAL) {
		*ntok = 2;
		return LEX_TOKEN_GREATER_EQUAL;
	}
	return t->kind;
}

VPRIV void cxx_skip_balanced(cxx *c)
{
	int depth = 0;

	do {
		switch (cxx_kind(c)) {
		case LEX_TOKEN_LEFT_PAREN: case LEX_TOKEN_LEFT_SQUARE: case LEX_TOKEN_LEFT_BRACE: depth++; break;
		case LEX_TOKEN_RIGHT_PAREN: case LEX_TOKEN_RIGHT_SQUARE: case LEX_TOKEN_RIGHT_BRACE: depth--; break;
		case LEX_TOKEN_EOF: cxx_fail(c, "unbalanced brackets");
		}
		cxx_advance(c);
	} while (depth > 0);
}

VPRIV void cxx_mark_set(cxx *c, cxx_mark *m)
{
	m->pos = c->pos, m->nundo = c->nundo, m->fail = c->fail, m->scope = c->scope;
	m->template_depth = c->template_depth, m->in_template_args = c->in_template_args;
	m->dependent_seen = c->dependent_seen;
}

VPRIV void cxx_mark_rewind(cxx *c, const cxx_mark *m)
{
	while (c->nundo > m->nundo) {
		c->nundo--;
		c->tok[c->undo[c->nundo].index] = c->undo[c->nundo].token;
	}
	c->pos = m->pos, c->fail = m->fail, c->scope = m->scope;
	c->template_depth = m->template_depth, c->in_template_args = m->in_template_args;
	c->dependent_seen = m->dependent_seen;	/* (what a rewound parse looked up does not count) */
}

/* back to pos, undoing the token edits made since (the undo log's length then) */
VPRIV void cxx_rewind(cxx *c, uint32_t pos, uint32_t nundo)
{
	while (c->nundo > nundo) {
		c->nundo--;
		c->tok[c->undo[c->nundo].index] = c->undo[c->nundo].token;
	}
	c->pos = pos;
}
